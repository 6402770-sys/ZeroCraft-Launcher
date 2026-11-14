#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# ZERO Setup build (Git Bash, MSYS2 UCRT64 toolchain at C:\msys64).
#   installer/build.sh dev            shared Qt  -> installer/build-dev/      (run with the MSYS2 DLLs on PATH)
#   installer/build.sh dev test       dev build + unit tests
#   installer/build.sh release        static Qt  -> installer/build-release/  (needs mingw-w64-ucrt-x86_64-qt6-static)
# Outputs: <build>/zero-setup.exe (the setup stub), <build>/zero-setup-pack.exe, <build>/ZeroSetupTests.exe (dev only).
# Env: ZERO_SETUP_VERSION=X.Y.Z stamps the exe's version resource; ZERO_SETUP_NO_UI=1 builds only core + pack + tests.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
export PATH="/c/msys64/ucrt64/bin:$PATH"
MODE="${1:-dev}"
EXTRA="${2:-}"
STATIC_PREFIX=/c/msys64/ucrt64/qt6-static

case "$MODE" in
  dev)
    BUILD=build-dev
    ARGS=(-DCMAKE_BUILD_TYPE=Debug -DZERO_SETUP_TESTS=ON)
    ;;
  release)
    BUILD=build-release
    if [ ! -f "$STATIC_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
      echo "Qt static is not installed ($STATIC_PREFIX)." >&2
      echo "Install it once from an MSYS2 UCRT64 shell:  pacman -S --needed mingw-w64-ucrt-x86_64-qt6-static" >&2
      exit 3
    fi
    ARGS=(-DCMAKE_BUILD_TYPE=Release -DZERO_SETUP_STATIC=ON -DZERO_SETUP_TESTS=OFF "-DCMAKE_PREFIX_PATH=$STATIC_PREFIX")
    ;;
  *) echo "usage: installer/build.sh dev|release [test]" >&2; exit 2 ;;
esac
if [ -n "${ZERO_SETUP_VERSION:-}" ]; then ARGS+=("-DZERO_SETUP_VERSION=$ZERO_SETUP_VERSION"); fi
if [ -n "${ZERO_SETUP_NO_UI:-}" ]; then ARGS+=(-DZERO_SETUP_BUILD_UI=OFF); else ARGS+=(-DZERO_SETUP_BUILD_UI=ON); fi

cmake -S . -B "$BUILD" -G Ninja "${ARGS[@]}" -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache > "$BUILD.configure.log" 2>&1 \
  || { tail -n 30 "$BUILD.configure.log"; exit 1; }
rm -f "$BUILD.configure.log"
cmake --build "$BUILD" 2>&1 | grep -E "error|Error|FAILED|warning:" | head -n 60 || true
cmake --build "$BUILD" > /dev/null   # real exit status

if [ "$MODE" = dev ] && [ "$EXTRA" = test ]; then
  rc=0
  # (QtTest's own text log: stdout of a Windows exe is not reliably captured by every shell)
  rm -f "$BUILD/tests.log"
  "$BUILD/ZeroSetupTests.exe" -o "$BUILD/tests.log,txt" > /dev/null 2>&1 || rc=$?
  grep -v "^PASS" "$BUILD/tests.log" | tail -n 80
  exit $rc
fi
echo "built: $ROOT/$BUILD/zero-setup.exe, $ROOT/$BUILD/zero-setup-pack.exe"
