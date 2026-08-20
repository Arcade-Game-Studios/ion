#!/usr/bin/env bash
#
# Ion Engine - interactive test runner.
#
# Detects the host OS, ensures the project is built, lists the available test
# binaries, lets the user pick one (or passes one directly on the command
# line), and runs it.
#
# Usage:
#   ./run_test.sh                  interactive picker
#   ./run_test.sh <name-or-number> run a specific test without a menu
#
# Examples:
#   ./run_test.sh core_test
#   ./run_test.sh 3
#   ./run_test.sh --no-build 2     skip (re)building and run test #2
#
set -euo pipefail

# ---------------------------------------------------------------------------
# OS detection
# ---------------------------------------------------------------------------
detect_os() {
    local os="unknown"
    case "$(uname -s)" in
        Linux*)
            os="linux"
            ;;
        Darwin*)
            os="macos"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            os="windows"
            ;;
    esac

    if [[ -n "${WSL_DISTRO_NAME:-}" ]]; then
        os="linux"
    fi

    echo "$os"
}

OS="$(detect_os)"

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"

# Binary file name extension per OS.
case "$OS" in
    windows) EXE_SUFFIX=".exe" ;;
    *)       EXE_SUFFIX="" ;;
esac

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
DO_BUILD=true
TARGET_ARG=""

for arg in "$@"; do
    case "$arg" in
        --no-build)
            DO_BUILD=false
            ;;
        -h|--help)
            echo "Usage: $0 [--no-build] [test-name|test-number]"
            exit 0
            ;;
        *)
            TARGET_ARG="$arg"
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
build() {
    local generator="Unix Makefiles"
    case "$OS" in
        macos) generator="Unix Makefiles" ;;
        windows) generator="MinGW Makefiles" ;;
    esac

    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        echo "==> Configuring build ($generator) ..."
        cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$generator" \
            -DCMAKE_BUILD_TYPE=Debug
    fi

    echo "==> Building ..."
    cmake --build "$BUILD_DIR" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
}

if [[ "$DO_BUILD" == true ]]; then
    build
fi

# ---------------------------------------------------------------------------
# Discover test binaries
# ---------------------------------------------------------------------------
# Only pick up executables that look like Ion tests/examples, in build order.
mapfile -t BINARIES < <(find "$BUILD_DIR" -maxdepth 1 -type f -perm -u+x \
    \( -name '*_test'"$EXE_SUFFIX" -o -name '*_example'"$EXE_SUFFIX" \) \
    2>/dev/null | sort)

if [[ ${#BINARIES[@]} -eq 0 ]]; then
    echo "error: no test binaries found in '$BUILD_DIR'" >&2
    echo "       (expected binaries matching '*_test' or '*_example')" >&2
    exit 1
fi

echo ""
echo "Ion Engine test runner ($OS)"
echo "---------------------------"

# ---------------------------------------------------------------------------
# Resolve target
# ---------------------------------------------------------------------------
resolve_target() {
    local name="$1"

    if [[ "$name" =~ ^[0-9]+$ ]]; then
        local idx=$((name - 1))
        if (( idx < 0 || idx >= ${#BINARIES[@]} )); then
            echo "error: invalid test number '$name' (1-${#BINARIES[@]})" >&2
            exit 1
        fi
        echo "${BINARIES[$idx]}"
        return
    fi

    local bin
    for bin in "${BINARIES[@]}"; do
        if [[ "$(basename "$bin")" == "$name" || "$(basename "$bin")" == "$name$EXE_SUFFIX" ]]; then
            echo "$bin"
            return
        fi
    done

    echo "error: no test named '$name'" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Menu
# ---------------------------------------------------------------------------
menu() {
    local i=1
    for bin in "${BINARIES[@]}"; do
        printf "  %2d) %s\n" "$i" "$(basename "$bin")"
        i=$((i + 1))
    done
    printf "   q) quit\n"
    echo ""
}

selected=""
if [[ -n "$TARGET_ARG" ]]; then
    selected="$(resolve_target "$TARGET_ARG")"
else
    menu
    read -r -p "Pick a test [1-${#BINARIES[@]}] (q to quit): " choice
    if [[ "$choice" == "q" || "$choice" == "Q" ]]; then
        echo "bye"
        exit 0
    fi
    if [[ -z "$choice" ]]; then
        choice=1
    fi
    selected="$(resolve_target "$choice")"
fi

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
echo ""
echo "==> Running: $(basename "$selected")"
echo ""
"$selected"
exit $?
