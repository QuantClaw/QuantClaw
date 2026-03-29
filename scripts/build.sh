#!/usr/bin/env bash
# QuantClaw build script
# Borrowed patterns from pikiwidb/build.sh: color output, CPU detection,
# option parsing, PM detection.
#
# Usage:
#   ./scripts/build.sh [options]
#
# Options:
#   -c, --clean     Wipe build directory before configuring
#   --debug         Build in Debug mode (default: Release)
#   --tests         Enable test targets (BUILD_TESTS=ON)
#   --asan          Enable AddressSanitizer (implies --debug)
#   --tsan          Enable ThreadSanitizer (implies --debug)
#   --ubsan         Enable UndefinedBehaviorSanitizer (implies --debug)
#   --no-sidecar    Skip Node.js sidecar build
#   -h, --help      Show this message

set -euo pipefail

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()    { echo -e "${CYAN}[build]${NC} $*"; }
success() { echo -e "${GREEN}[build]${NC} $*"; }
warn()    { echo -e "${YELLOW}[build]${NC} $*"; }
die()     { echo -e "${RED}[build] ERROR:${NC} $*" >&2; exit 1; }

# ── CPU cores ────────────────────────────────────────────────────────────────
detect_cores() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sysctl -n hw.logicalcpu
    elif [[ -f /proc/cpuinfo ]]; then
        grep -c ^processor /proc/cpuinfo
    else
        echo 4
    fi
}
CPU_CORES=$(detect_cores)

# ── Defaults ─────────────────────────────────────────────────────────────────
BUILD_TYPE=Release
BUILD_TESTS=OFF
CLEAN=0
SANITIZER=""
BUILD_SIDECAR=1

# ── Argument parsing ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--clean)      CLEAN=1 ;;
        --debug)         BUILD_TYPE=Debug ;;
        --tests)         BUILD_TESTS=ON ;;
        --asan)          SANITIZER=asan; BUILD_TYPE=Debug ;;
        --tsan)          SANITIZER=tsan; BUILD_TYPE=Debug ;;
        --ubsan)         SANITIZER=ubsan; BUILD_TYPE=Debug ;;
        --no-sidecar)    BUILD_SIDECAR=0 ;;
        -h|--help)
            sed -n '/^# Usage:/,/^[^#]/p' "$0" | sed 's/^# \{0,2\}//' | head -n -1
            exit 0 ;;
        *)
            die "Unknown option: $1 (use -h for help)" ;;
    esac
    shift
done

# ── Repo root (one level up from scripts/) ───────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT/build"

# ── Dependency check ─────────────────────────────────────────────────────────
check_dep() {
    local cmd="$1" apt_pkg="$2" brew_pkg="${3:-$2}" pacman_pkg="${4:-$2}"
    command -v "$cmd" &>/dev/null && return 0
    warn "'$cmd' not found — attempting to install..."
    if command -v apt-get &>/dev/null; then
        sudo apt-get update -qq && sudo apt-get install -y "$apt_pkg"
    elif command -v brew &>/dev/null; then
        brew install "$brew_pkg"
    elif command -v dnf &>/dev/null; then
        sudo dnf install -y "$apt_pkg"
    elif command -v pacman &>/dev/null; then
        sudo pacman -S --noconfirm "$pacman_pkg"
    else
        die "Please install '$cmd' manually."
    fi
}

ensure_brew() {
    command -v brew &>/dev/null || die "Homebrew is required on macOS."
}

ensure_brew_package() {
    local pkg="$1"
    brew list --versions "$pkg" &>/dev/null && return 0
    info "Installing missing Homebrew package: $pkg"
    brew install "$pkg"
}

CMAKE_EXTRA_ARGS=()

if [[ "$OSTYPE" == "darwin"* ]]; then
    ensure_brew
    for pkg in cmake ninja pkg-config git spdlog openssl@3 curl; do
        ensure_brew_package "$pkg"
    done
    if [[ $BUILD_SIDECAR -eq 1 ]]; then
        ensure_brew_package node
    fi

    HOMEBREW_PREFIX="$(brew --prefix)"
    OPENSSL_PREFIX="$(brew --prefix openssl@3)"
    CURL_PREFIX="$(brew --prefix curl)"
    export CMAKE_PREFIX_PATH="${HOMEBREW_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
    export PKG_CONFIG_PATH="${OPENSSL_PREFIX}/lib/pkgconfig:${CURL_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
    CMAKE_EXTRA_ARGS+=(
        "-DOPENSSL_ROOT_DIR=${OPENSSL_PREFIX}"
        "-DCURL_ROOT=${CURL_PREFIX}"
    )
else
    check_dep git git git git
    check_dep cmake cmake cmake cmake
    check_dep ninja ninja-build ninja ninja
fi

# ── Clean ─────────────────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    info "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# ── Sanitizer flags ───────────────────────────────────────────────────────────
SANITIZER_FLAG=""
case "$SANITIZER" in
    asan)  SANITIZER_FLAG="-DENABLE_ASAN=ON" ;;
    tsan)  SANITIZER_FLAG="-DENABLE_TSAN=ON" ;;
    ubsan) SANITIZER_FLAG="-DENABLE_UBSAN=ON" ;;
esac

# ── CMake configure ───────────────────────────────────────────────────────────
info "Configuring CMake ($BUILD_TYPE, tests=$BUILD_TESTS, cores=$CPU_CORES)..."
cmake -B "$BUILD_DIR" -S "$ROOT" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_CXX_STANDARD=17 \
    -DBUILD_TESTS="$BUILD_TESTS" \
    ${SANITIZER_FLAG:+"$SANITIZER_FLAG"} \
    "${CMAKE_EXTRA_ARGS[@]}"

# ── C++ build ─────────────────────────────────────────────────────────────────
info "Building C++ (${CPU_CORES} cores)..."
cmake --build "$BUILD_DIR" --parallel "$CPU_CORES"

# ── Sidecar (Node.js) ─────────────────────────────────────────────────────────
if [[ $BUILD_SIDECAR -eq 1 ]]; then
    SIDECAR_DIR="$ROOT/sidecar"
    if [[ -f "$SIDECAR_DIR/package.json" ]]; then
        if ! command -v node &>/dev/null; then
            die "node not found — install Node.js or use --no-sidecar"
        else
            info "Building sidecar..."
            cd "$SIDECAR_DIR"
            npm ci
            npm run build
            cd "$ROOT"
        fi
    fi
fi

# ── Done ──────────────────────────────────────────────────────────────────────
BINARY="$BUILD_DIR/quantclaw"
if [[ -f "$BINARY" ]]; then
    SIZE=$(du -sh "$BINARY" | cut -f1)
    success "Build complete → $BINARY ($SIZE)"
    if [[ $BUILD_TESTS == ON ]]; then
        info "Run tests with: cd build && ctest --output-on-failure"
    fi
else
    die "Binary not found at $BINARY — build may have failed."
fi
