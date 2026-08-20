#!/usr/bin/env bash
#
# update-ion.sh - Update the Ion Engine to the latest version.
#
# Usage:
#   cd /path/to/ion-engine && ./update-ion.sh     # rebuild local source
#   cd /path/to/my_game    && ./update-ion.sh --local  # update bundled ion/
#
set -euo pipefail

REPO_URL="https://github.com/Arcade-Game-Studios/ion.git"
ION_PREFIX="${ION_PREFIX:-$HOME/.ion}"

get_version() {
    sed -n 's/.*project(ion VERSION \([0-9.]*\)).*/\1/p' "$1/CMakeLists.txt" 2>/dev/null || echo "unknown"
}

NCPUS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# --local mode: update ion/ bundled inside a game project
if [[ "${1:-}" == "--local" ]]; then
    if [[ ! -d "ion" ]]; then
        echo "error: no ion/ directory in $(pwd). Run this from your game project root." >&2
        exit 1
    fi

    OLD=$(get_version ion)

    echo "==> Updating ion/ (was v$OLD)..."
    if [[ -d "ion/.git" ]]; then
        git -C ion pull --rebase 2>&1
    else
        echo "==> ion/ is not a git repo, re-cloning from GitHub..."
        TMP=$(mktemp -d)
        git clone --depth 1 "$REPO_URL" "$TMP/ion" 2>&1
        rm -rf ion
        mv "$TMP/ion" ion
        rm -rf "$TMP"
    fi

    NEW=$(get_version ion)
    echo "==> Updated v$OLD -> v$NEW"

    echo "==> Rebuilding..."
    rm -rf build
    mkdir build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1
    cmake --build . -j"$NCPUS" 2>&1
    echo ""
    echo "==> Done! Game updated to Ion v$NEW"
    exit 0
fi

# Default mode: rebuild current local source
if [[ ! -f "CMakeLists.txt" ]]; then
    echo "error: CMakeLists.txt not found in $(pwd). Run from an ion-engine directory." >&2
    exit 1
fi

VERSION=$(get_version .)
echo "==> Building Ion Engine v$VERSION (Release)..."
BUILD_DIR="build-release"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DION_BUILD_EXAMPLES=OFF -DION_BUILD_TESTS=OFF 2>&1
cmake --build "$BUILD_DIR" --config Release -j"$NCPUS" 2>&1

echo "==> Installing to $ION_PREFIX..."
cmake --install "$BUILD_DIR" --prefix "$ION_PREFIX" 2>&1

echo "==> Installing starter template..."
mkdir -p "$ION_PREFIX/template"
cp template/CMakeLists.txt "$ION_PREFIX/template/"
cp template/main.cpp "$ION_PREFIX/template/"

echo ""
echo "==> Ion Engine v$VERSION installed to $ION_PREFIX"
