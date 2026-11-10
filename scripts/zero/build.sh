#!/usr/bin/env bash
# ZERO Launcher dev build (Windows, MSYS2 UCRT64 toolchain installed at C:\msys64).
# Usage (from Git Bash at repo root):
#   scripts/zero/build.sh            # configure (first time) + build
#   scripts/zero/build.sh test       # build + run unit tests
#   scripts/zero/build.sh run        # build + launch the app
set -euo pipefail
cd "$(dirname "$0")/../.."

export PATH="/c/msys64/ucrt64/bin:$PATH"
BUILD_DIR=build

if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_LTO=OFF \
    -DBUILD_TESTING=ON \
    -DLauncher_USE_PCH=ON \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
fi

cmake --build "$BUILD_DIR" -- -k 0 2>&1 | grep -E "error|Error|FAILED|warning: unused|^\[[0-9]+/[0-9]+\] Linking" | tail -n 60 || true
# Re-run quietly to get the real exit status (no-op if the build succeeded).
cmake --build "$BUILD_DIR" > /dev/null

case "${1:-}" in
  test)
    # FileSystem is excluded: its symlink test spawns the *_filelink.exe helper elevated (UAC),
    # which hangs without Developer Mode and shows missing-DLL dialogs (elevated env lacks MSYS2 PATH).
    ctest --test-dir "$BUILD_DIR" -E "^FileSystem$" --timeout 120 -j4 --output-on-failure 2>&1 | tail -n 40
    # FileSystem minus its 7 symlink cases (test_link*): those need symlink privileges and launch the elevated helper.
    fs_out="$BUILD_DIR/filesystem-partial.txt"
    timeout 120 "$BUILD_DIR/LauncherTests.exe" FileSystem_test -o "$fs_out,txt" \
      test_pathCombine test_PathCombine1 test_PathCombine2 test_copy test_copy_with_blacklist test_copy_with_whitelist \
      test_copy_with_dot_hidden test_copy_single_file test_getDesktop test_hard_link test_path_depth test_path_trunc \
      > /dev/null 2>&1 || true
    echo "FileSystem (without symlink cases): $(grep -E '^Totals' "$fs_out" 2>/dev/null || echo 'no result')"
    ;;
  run)
    exe=$(ls "$BUILD_DIR"/*.exe | grep -v -i -E "test|updater|filelink" | head -n 1)
    "$exe" &
    ;;
esac
