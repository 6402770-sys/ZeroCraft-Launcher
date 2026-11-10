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
    ;;
  run)
    exe=$(ls "$BUILD_DIR"/*.exe | grep -v -i -E "test|updater|filelink" | head -n 1)
    "$exe" &
    ;;
esac
