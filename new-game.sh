#!/usr/bin/env bash
#
# new-game.sh - Create a new game project with Ion bundled.
#
# Usage:
#   ./new-game.sh my_cool_game
#   ./new-game.sh /Volumes/SSD/Stroke\ Cube
#
set -euo pipefail

GAME_PATH="${1:?Usage: ./new-game.sh <path_or_name>}"

if [[ "$GAME_PATH" = /* ]]; then
    DEST="$GAME_PATH"
else
    DEST="$(pwd)/$GAME_PATH"
fi

if [[ -d "$DEST" ]]; then
    echo "error: $DEST already exists" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "==> Creating $DEST"
mkdir -p "$DEST"
cp "$SCRIPT_DIR/template-local/main.cpp" "$DEST/"
cp "$SCRIPT_DIR/template-local/CMakeLists.txt" "$DEST/"
cp "$SCRIPT_DIR/template-local/Makefile" "$DEST/"
cp -R "$SCRIPT_DIR/template-local/.vscode" "$DEST/"

echo "==> Copying ion engine source..."
mkdir -p "$DEST/ion"

# Root build file
cp "$SCRIPT_DIR/CMakeLists.txt" "$DEST/ion/"

# Source files
cp -R "$SCRIPT_DIR/core" "$DEST/ion/"
cp -R "$SCRIPT_DIR/platform" "$DEST/ion/"

# Headers
cp -R "$SCRIPT_DIR/include" "$DEST/ion/"

# Third-party (stb_truetype etc)
cp -R "$SCRIPT_DIR/third_party" "$DEST/ion/"

# Assets (window icon etc)
if [[ -d "$SCRIPT_DIR/assets" ]]; then
    cp -R "$SCRIPT_DIR/assets" "$DEST/ion/"
fi

# CMake helper
mkdir -p "$DEST/ion/cmake"
cp "$SCRIPT_DIR/cmake/ion-config.cmake" "$DEST/ion/cmake/"

echo ""
echo "==> Done! To build:"
echo "    cd \"$DEST\""
echo "    mkdir build && cd build"
echo "    cmake .. -DCMAKE_BUILD_TYPE=Debug"
echo "    cmake --build . && ./my_game"
