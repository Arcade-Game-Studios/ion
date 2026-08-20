#!/usr/bin/env bash
#
# install-ion.sh - Install the Ion Engine for game development.
#
# Usage:
#   ./install-ion.sh                 # install from current directory
#   ./install-ion.sh /path/to/ion    # install from a specific path
#
# Installs to ~/.ion by default. Override with ION_PREFIX.
#
set -euo pipefail

ION_PREFIX="${ION_PREFIX:-$HOME/.ion}"
REPO_URL="https://github.com/Arcade-Game-Studios/ion.git"
NCPUS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

if [[ "${1:-}" != "" ]]; then
    ENGINE_DIR="$(cd "$1" && pwd)"
else
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    if [[ -f "$SCRIPT_DIR/CMakeLists.txt" ]]; then
        ENGINE_DIR="$SCRIPT_DIR"
    else
        echo "==> No local source found, cloning from GitHub..."
        ENGINE_DIR="${ION_PREFIX}/.src"
        rm -rf "$ENGINE_DIR"
        git clone --depth 1 "$REPO_URL" "$ENGINE_DIR" 2>&1
    fi
fi

if [[ ! -f "$ENGINE_DIR/CMakeLists.txt" ]]; then
    echo "error: CMakeLists.txt not found in $ENGINE_DIR" >&2
    exit 1
fi

VERSION=$(sed -n 's/.*project(ion VERSION \([0-9.]*\)).*/\1/p' "$ENGINE_DIR/CMakeLists.txt")
BUILD_DIR="$ENGINE_DIR/build-release"

echo "==> Building Ion Engine v$VERSION (Release)..."
cmake -S "$ENGINE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DION_BUILD_EXAMPLES=OFF -DION_BUILD_TESTS=OFF 2>&1
cmake --build "$BUILD_DIR" --config Release -j"$NCPUS" 2>&1

echo "==> Installing to $ION_PREFIX..."
cmake --install "$BUILD_DIR" --prefix "$ION_PREFIX" 2>&1

echo "==> Installing starter template..."
mkdir -p "$ION_PREFIX/template"
cp "$ENGINE_DIR/template/CMakeLists.txt" "$ION_PREFIX/template/"
cp "$ENGINE_DIR/template/main.cpp" "$ION_PREFIX/template/"

echo ""
echo "==> Ion Engine v$VERSION installed to $ION_PREFIX"
echo ""
echo "To start a new game:"
echo "    mkdir my_game && cd my_game"
echo "    cp $ION_PREFIX/template/* ."
echo "    mkdir build && cd build"
echo "    cmake .. -DCMAKE_BUILD_TYPE=Debug"
echo "    cmake --build . && ./my_game"
