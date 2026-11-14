#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# ZERO Launcher release packaging (Windows, MSYS2 UCRT64 toolchain installed at C:\msys64).
#
# Usage (Git Bash, repo root):
#   scripts/zero/package.sh [--version X.Y.Z] [--preview | --release] [--no-installer] [--no-smoke]
#
#   --version X.Y.Z   Version to build. Default: the version already in the root CMakeLists.txt.
#   --preview         (default) Build with the development credentials (Prism's MSA client id / CurseForge key).
#                     Fine for testing the package, NOT for a public release.
#   --release         Official build: passes -DZERO_RELEASE_BUILD=ON. Needs the CurseForge key: env var ZERO_CURSEFORGE_API_KEY,
#                     else the file ~/.zero/curseforge-api-key (or the path in ZERO_CURSEFORGE_API_KEY_FILE); the
#                     env var ZERO_MSA_CLIENT_ID is optional (unset = Prism's MSA client ID, the owner's choice for now)
#                     (never echoed; see docs/zero/CREDENTIALS.md). The values end up in
#                     build-release/CMakeCache.txt, which is git-ignored: never copy that folder anywhere.
#   --no-installer    Skip the installer (ZERO Setup and NSIS).
#   --nsis            Build the old NSIS installer instead of ZERO Setup (also the automatic fallback when the static Qt
#                     of installer/ is not installed: pacman -S --needed mingw-w64-ucrt-x86_64-qt6-static).
#   --no-smoke        Skip the start-up test of the finished portable zip.
#
# What it does:
#   1. Configures + builds a Release tree in build-release/ (Ninja, ccache) with the updater enabled.
#   2. `cmake --install` into dist/stage/ZEROLauncher/ (Prism's deploy script: windeployqt + runtime dependencies),
#      then makes the folder self-contained: windeployqt fallback for missing Qt plugins, and every non-system DLL
#      reachable from the exe / DLLs / plugins is copied from C:\msys64\ucrt64\bin (objdump -p, recursively).
#   3. Writes into dist/ (git-ignored):
#        ZEROLauncher-<ver>-Windows-Portable.zip   (stage + portable.txt, flat, no top folder)
#        ZEROLauncher-<ver>-Windows-Setup.exe      (ZERO Setup, installer/: one static exe + payload; else NSIS when makensis is found)
#        SHA256SUMS.txt
#   4. Smoke test: extracts the zip to a temp folder and starts it with PATH = Windows only, a temp data dir (-d) and
#      --zero-screenshot; it must exit 0 and write a PNG.
#
# Updater: Launcher_UPDATER_GITHUB_REPO=https://github.com/6402770-sys/ZeroCraft-Launcher, Launcher_BUILD_ARTIFACT=Windows.
# The updater (launcher/updater/prismupdater) accepts a release asset when its lower-cased name contains the artifact
# ("windows"), contains "portable" for a portable install and is NOT an archive for an installed one. The file names above
# follow that rule: upload both files (and SHA256SUMS.txt) to the GitHub release.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD

MSYS_BIN=/c/msys64/ucrt64/bin
export PATH="$MSYS_BIN:$PATH"

REPO_URL="https://github.com/6402770-sys/ZeroCraft-Launcher"
ARTIFACT="Windows"
PLATFORM="Windows-MinGW-w64"
BUILD_DIR=build-release
DIST="$ROOT/dist"
STAGE="$DIST/stage/ZEROLauncher"

VERSION=""
MODE=preview
DO_INSTALLER=1
FORCE_NSIS=0
DO_SMOKE=1
while [ $# -gt 0 ]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    --preview) MODE=preview; shift ;;
    --release) MODE=release; shift ;;
    --no-installer) DO_INSTALLER=0; shift ;;
    --nsis) FORCE_NSIS=1; shift ;;
    --no-smoke) DO_SMOKE=0; shift ;;
    -h|--help) sed -n '2,34p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
  esac
done

cmake_version() {
  local k out=""
  for k in MAJOR MINOR PATCH; do
    out="$out$(sed -n "s/^set(Launcher_VERSION_$k \([0-9][0-9]*\))\r\{0,1\}\$/\1/p" CMakeLists.txt | head -n 1)."
  done
  echo "${out%.}"
}
VERSION_CMAKE=$(cmake_version)
[ -n "$VERSION" ] || VERSION="$VERSION_CMAKE"
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "version must look like X.Y.Z, got '$VERSION'" >&2; exit 2
fi
echo "== ZERO packaging: version $VERSION (CMakeLists has $VERSION_CMAKE), mode $MODE"

# ---------------------------------------------------------------- configure
CMAKE_ARGS=(
  -S . -B "$BUILD_DIR" -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DENABLE_LTO=OFF
  -DBUILD_TESTING=OFF
  -DLauncher_USE_PCH=ON
  -DCMAKE_C_COMPILER_LAUNCHER=ccache
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  "-DCMAKE_INSTALL_PREFIX=$STAGE"
  "-DLauncher_BUILD_PLATFORM=$PLATFORM"
  "-DLauncher_UPDATER_GITHUB_REPO=$REPO_URL"
  "-DLauncher_BUILD_ARTIFACT=$ARTIFACT"
  -ULauncher_ELY_CLIENT_ID  # ZERO's Ely.by client ID (the CMakeLists.txt default), even in an older cache
  "-DZERO_VERSION=$VERSION"
)
if [ "$MODE" = release ]; then
  # The key comes from the env var, else from a private file outside the repo (never from the repo, never printed).
  CF_KEY_FILE="${ZERO_CURSEFORGE_API_KEY_FILE:-$HOME/.zero/curseforge-api-key}"
  if [ -z "${ZERO_CURSEFORGE_API_KEY:-}" ] && [ -s "$CF_KEY_FILE" ]; then
    ZERO_CURSEFORGE_API_KEY="$(tr -d '\r\n' < "$CF_KEY_FILE")"
  fi
  if [ -z "${ZERO_CURSEFORGE_API_KEY:-}" ]; then
    echo "--release needs the CurseForge key: env var ZERO_CURSEFORGE_API_KEY or the file $CF_KEY_FILE (see docs/zero/CREDENTIALS.md)." >&2
    exit 2
  fi
  CMAKE_ARGS+=(-DZERO_RELEASE_BUILD=ON "-DLauncher_CURSEFORGE_API_KEY=$ZERO_CURSEFORGE_API_KEY")
  if [ -n "${ZERO_MSA_CLIENT_ID:-}" ]; then
    CMAKE_ARGS+=("-DLauncher_MSA_CLIENT_ID=$ZERO_MSA_CLIENT_ID")
  else
    CMAKE_ARGS+=(-ULauncher_MSA_CLIENT_ID)  # Prism's MSA client ID (the CMakeLists.txt default)
  fi
else
  # Drop credentials a previous --release run left in the cache so a preview never carries them.
  CMAKE_ARGS+=(-DZERO_RELEASE_BUILD=OFF -ULauncher_MSA_CLIENT_ID -ULauncher_CURSEFORGE_API_KEY)
fi
cmake "${CMAKE_ARGS[@]}" > "$BUILD_DIR.configure.log" 2>&1 || { tail -n 30 "$BUILD_DIR.configure.log"; exit 1; }
rm -f "$BUILD_DIR.configure.log"

# ---------------------------------------------------------------- build
echo "== building (Release)"
cmake --build "$BUILD_DIR" 2>&1 | grep -E "error|Error|FAILED|^\[[0-9]+/[0-9]+\] Linking" | tail -n 40 || true
cmake --build "$BUILD_DIR" > /dev/null   # real exit status (no-op when the build succeeded)

# ---------------------------------------------------------------- stage
echo "== installing into dist/stage"
case "$STAGE" in "$ROOT"/dist/stage/ZEROLauncher) rm -rf "$STAGE" ;; *) echo "bad stage path" >&2; exit 1 ;; esac
mkdir -p "$DIST"
# Prism's install scripts print many CMake policy warnings; show stderr only when the install fails.
cmake --install "$BUILD_DIR" --strip > /dev/null 2> "$DIST/install.err" || { tail -n 30 "$DIST/install.err" >&2; exit 1; }
rm -f "$DIST/install.err"
[ -f "$STAGE/zerolauncher.exe" ] || { echo "install did not produce zerolauncher.exe" >&2; exit 1; }

# Qt plugins the app needs. Prism's deploy script skips a few plugin types, so check and fall back to windeployqt.
need_plugins() {
  local p missing=0
  for p in platforms/qwindows.dll styles/qmodernwindowsstyle.dll tls/qschannelbackend.dll \
           imageformats/qjpeg.dll imageformats/qgif.dll imageformats/qsvg.dll imageformats/qwebp.dll \
           iconengines/qsvgicon.dll multimedia/ffmpegmediaplugin.dll; do
    [ -f "$STAGE/$p" ] || { echo "   missing plugin: $p"; missing=1; }
  done
  return $missing
}
if ! need_plugins; then
  echo "== running windeployqt6 for the missing plugins"
  windeployqt6 --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler \
    --no-quick-import --skip-plugin-types generic,networkinformation \
    --dir "$STAGE" "$STAGE/zerolauncher.exe" > /dev/null
  need_plugins || { echo "plugins still missing after windeployqt6" >&2; exit 1; }
fi

# Self-contained: copy every non-system DLL the exe, the DLLs and the plugins import from the MSYS2 bin folder.
echo "== resolving DLL dependencies (objdump)"
declare -A HAVE=()
while IFS= read -r f; do HAVE["$(basename "$f" | tr 'A-Z' 'a-z')"]=1; done < <(find "$STAGE" -maxdepth 1 -iname '*.dll')
QUEUE=()
while IFS= read -r f; do QUEUE+=("$f"); done < <(find "$STAGE" \( -iname '*.dll' -o -iname '*.exe' \))
MISSING=()
COPIED=0
while [ ${#QUEUE[@]} -gt 0 ]; do
  cur="${QUEUE[0]}"; QUEUE=("${QUEUE[@]:1}")
  while IFS= read -r dll; do
    low=$(echo "$dll" | tr 'A-Z' 'a-z')
    [ -z "${HAVE[$low]:-}" ] || continue
    case "$low" in api-ms-win-*|ext-ms-*) continue ;; esac
    if [ -e "/c/Windows/System32/$dll" ]; then HAVE[$low]=1; continue; fi   # Windows ships it
    if [ -e "$MSYS_BIN/$dll" ]; then
      cp "$MSYS_BIN/$dll" "$STAGE/$dll"; HAVE[$low]=1; COPIED=$((COPIED + 1)); QUEUE+=("$STAGE/$dll")
    else
      HAVE[$low]=1; MISSING+=("$dll (needed by $(basename "$cur"))")
    fi
  done < <(objdump -p "$cur" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name: //p' | tr -d '\r')
done
echo "   copied $COPIED DLLs from $MSYS_BIN"
if [ ${#MISSING[@]} -gt 0 ]; then
  printf '   UNRESOLVED: %s\n' "${MISSING[@]}" >&2
  echo "some DLL imports could not be found: the package would not start on a clean PC" >&2
  exit 1
fi
ls "$STAGE" | grep -i -E '^(libav|libsw|Qt6Multimedia)' > /dev/null || { echo "multimedia / ffmpeg DLLs missing" >&2; exit 1; }

# ---------------------------------------------------------------- portable zip
PORTABLE="ZEROLauncher-$VERSION-Windows-Portable.zip"
SETUP="ZEROLauncher-$VERSION-Windows-Setup.exe"
rm -f "$DIST/$PORTABLE" "$DIST/$SETUP" "$DIST/SHA256SUMS.txt"

# Installer first (it must not contain portable.txt), then the portable zip.
build_nsis() {
  MAKENSIS=$(command -v makensis || true)
  for c in "/c/Program Files (x86)/NSIS/makensis.exe" "/c/Program Files/NSIS/makensis.exe"; do
    [ -n "$MAKENSIS" ] || { [ -x "$c" ] && MAKENSIS="$c"; }
  done
  if [ -z "$MAKENSIS" ]; then
    echo "== installer skipped: makensis (NSIS) not found."
    echo "   Install it once with:  pacman -S --needed mingw-w64-ucrt-x86_64-nsis   (MSYS2 UCRT64 shell), then run this script again."
  else
    echo "== building the installer with $MAKENSIS"
    NSI="$STAGE/win_install.nsi"
    cp "$BUILD_DIR/program_info/win_install.nsi" "$NSI"
    # The template writes "../<name>-Setup.exe" and "../program_info/..ico" relative to a staging folder: make them absolute.
    ICO_WIN=$(cygpath -m "$ROOT/program_info/zerolauncher.ico")
    OUT_WIN=$(cygpath -m "$DIST/$SETUP")
    sed -i -e "s|^OutFile .*|OutFile \"$OUT_WIN\"|" -e "s|^!define MUI_ICON .*|!define MUI_ICON \"$ICO_WIN\"|" "$NSI"
    # every top-level folder of the stage must be packed by the template
    for d in "$STAGE"/*/; do
      n=$(basename "$d")
      grep -q "File .*\"$n\"" "$NSI" || { echo "win_install.nsi.in does not pack the folder '$n'" >&2; rm -f "$NSI"; exit 1; }
    done
    (cd "$STAGE" && MSYS2_ARG_CONV_EXCL='*' "$MAKENSIS" -V2 win_install.nsi) || { rm -f "$NSI"; exit 1; }
    rm -f "$NSI"
  fi
}

# ZERO Setup (installer/): the static setup exe + the payload (this stage folder) appended by zero-setup-pack.
STATIC_QT=/c/msys64/ucrt64/qt6-static
build_zero_setup() {
  echo "== building ZERO Setup (installer/, static Qt)"
  ZERO_SETUP_VERSION="$VERSION" "$ROOT/installer/build.sh" release > "$DIST/installer-build.log" 2>&1 || { tail -n 30 "$DIST/installer-build.log" >&2; return 1; }
  rm -f "$DIST/installer-build.log"
  local stub="$ROOT/installer/build-release/zero-setup.exe" pack="$ROOT/installer/build-release/zero-setup-pack.exe"
  "$pack" --stub "$(cygpath -m "$stub")" --stage "$(cygpath -m "$STAGE")" --version "$VERSION" --out "$(cygpath -m "$DIST/$SETUP")" > /dev/null || return 1
  "$pack" --info "$(cygpath -m "$DIST/$SETUP")" || return 1
  # one exe: only Windows' own DLLs may be imported
  local bad
  bad=$(objdump -p "$stub" | sed -n 's/^[[:space:]]*DLL Name: //p' | tr -d '
' | while read -r d; do
    low=$(echo "$d" | tr 'A-Z' 'a-z')
    case "$low" in api-ms-win-*|ext-ms-*) continue ;; esac
    [ -e "/c/Windows/System32/$d" ] || echo "$d"
  done)
  if [ -n "$bad" ]; then echo "ZERO Setup imports non-system DLLs: $bad" >&2; return 1; fi
}

if [ "$DO_INSTALLER" = 1 ]; then
  if [ "$FORCE_NSIS" = 0 ] && [ -f "$STATIC_QT/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    build_zero_setup || { echo "ZERO Setup failed" >&2; exit 1; }
  else
    if [ "$FORCE_NSIS" = 0 ]; then
      echo "== NOTICE: static Qt is not installed ($STATIC_QT), so the NSIS installer is built instead of ZERO Setup."
      echo "   For the animated ZERO Setup run once (MSYS2 UCRT64 shell):  pacman -S --needed mingw-w64-ucrt-x86_64-qt6-static"
    fi
    build_nsis
  fi
fi

echo "== portable zip"
cp "$ROOT/program_info/portable.txt" "$STAGE/portable.txt"
(cd "$STAGE" && cmake -E tar cf "$(cygpath -m "$DIST/$PORTABLE")" --format=zip -- *)
rm -f "$STAGE/portable.txt"

(cd "$DIST" && sha256sum ZEROLauncher-"$VERSION"-Windows-*.* > SHA256SUMS.txt)

# ---------------------------------------------------------------- smoke test
if [ "$DO_SMOKE" = 1 ]; then
  echo "== smoke test: portable zip on a Windows-only PATH"
  T=$(mktemp -d "${TEMP:-/tmp}/zero-smoke-XXXXXX")
  mkdir -p "$T/app" "$T/data"
  (cd "$T/app" && cmake -E tar xf "$(cygpath -m "$DIST/$PORTABLE")")
  PNG="$T/smoke.png"
  rc=0
  env PATH="/c/Windows/System32:/c/Windows" /usr/bin/timeout 90 "$T/app/zerolauncher.exe" \
    -d "$(cygpath -m "$T/data")" --zero-screenshot "$(cygpath -m "$PNG")" --zero-page home > "$T/run.log" 2>&1 || rc=$?
  if [ "$rc" != 0 ] || [ ! -s "$PNG" ]; then
    echo "smoke test FAILED (exit $rc, png: $([ -s "$PNG" ] && echo yes || echo no)); log: $T/run.log" >&2
    tail -n 15 "$T/run.log" >&2 || true
    exit 1
  fi
  echo "   OK: starts and renders without MSYS2 on PATH ($(stat -c %s "$PNG") byte screenshot)"
  rm -rf "$T"
fi

echo "== done"
ls -l "$DIST" | grep -v '^total' | grep -v ' stage$'
echo "   stage folder: $(du -sm "$STAGE" | cut -f1) MB, $(find "$STAGE" -iname '*.dll' | wc -l) DLLs"
