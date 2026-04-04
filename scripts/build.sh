#!/usr/bin/env bash
# QuantClaw build helper for the C++23 modules fork.
#
# Defaults:
#   - configure preset: gcc16-ninja
#   - build type: Release
#   - tests: ON
#
# Examples:
#   ./scripts/build.sh
#   ./scripts/build.sh --debug --tests
#   ./scripts/build.sh -c --asan
#   ./scripts/build.sh --release --no-tests -j 8

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()    { echo -e "${CYAN}[build]${NC} $*"; }
success() { echo -e "${GREEN}[build]${NC} $*"; }
warn()    { echo -e "${YELLOW}[build]${NC} $*"; }
die()     { echo -e "${RED}[build] ERROR:${NC} $*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

PRESET="gcc16-ninja"
BUILD_DIR="${ROOT}/build-cmake43"
BUILD_TYPE="Release"
BUILD_TESTS="ON"
ENABLE_ASAN="OFF"
ENABLE_TSAN="OFF"
ENABLE_UBSAN="OFF"
ENABLE_COVERAGE="OFF"
CLEAN="false"
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
    cat <<EOF
Usage: ./scripts/build.sh [options]

Options:
  -c, --clean         Remove build directory before configuring
      --debug         Debug build
      --release       Release build (default)
      --tests         Build tests (default)
      --no-tests      Skip test targets
      --asan          Enable AddressSanitizer
      --tsan          Enable ThreadSanitizer
      --ubsan         Enable UndefinedBehaviorSanitizer
      --coverage      Enable gcov coverage instrumentation
  -j, --jobs <N>      Parallel build jobs (default: ${JOBS})
  -h, --help          Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--clean)
            CLEAN="true"
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --tests)
            BUILD_TESTS="ON"
            shift
            ;;
        --no-tests)
            BUILD_TESTS="OFF"
            shift
            ;;
        --asan)
            ENABLE_ASAN="ON"
            ENABLE_TSAN="OFF"
            shift
            ;;
        --tsan)
            ENABLE_TSAN="ON"
            ENABLE_ASAN="OFF"
            shift
            ;;
        --ubsan)
            ENABLE_UBSAN="ON"
            shift
            ;;
        --coverage)
            ENABLE_COVERAGE="ON"
            shift
            ;;
        -j|--jobs)
            [[ $# -ge 2 ]] || die "Missing value for $1"
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

if [[ "${ENABLE_ASAN}" == "ON" && "${ENABLE_TSAN}" == "ON" ]]; then
    die "--asan and --tsan are mutually exclusive"
fi

if [[ "${CLEAN}" == "true" ]]; then
    info "Cleaning ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

QC_CC="gcc"
QC_CXX="g++"
if command -v gcc-16 >/dev/null 2>&1 && command -v g++-16 >/dev/null 2>&1; then
    QC_CC="gcc-16"
    QC_CXX="g++-16"
fi

command -v "${QC_CC}" >/dev/null 2>&1 || die "${QC_CC} not found"
command -v "${QC_CXX}" >/dev/null 2>&1 || die "${QC_CXX} not found"

GCC_VERSION="$(${QC_CXX} -dumpfullversion 2>/dev/null || ${QC_CXX} -dumpversion 2>/dev/null || true)"
GCC_MAJOR="${GCC_VERSION%%.*}"
[[ -n "${GCC_MAJOR}" ]] || die "Failed to detect GCC version from ${QC_CXX}"
if (( GCC_MAJOR < 15 )); then
    die "GCC 15+ is required for this fork (detected ${QC_CXX} ${GCC_VERSION})"
fi

info "Compiler: ${QC_CXX} (${GCC_VERSION})"
info "Preset:   ${PRESET}"
info "Type:     ${BUILD_TYPE}"
info "Tests:    ${BUILD_TESTS}"

if [[ "${ENABLE_ASAN}" == "ON" ]]; then
    warn "ASAN enabled"
fi
if [[ "${ENABLE_TSAN}" == "ON" ]]; then
    warn "TSAN enabled"
fi
if [[ "${ENABLE_UBSAN}" == "ON" ]]; then
    warn "UBSAN enabled"
fi
if [[ "${ENABLE_COVERAGE}" == "ON" ]]; then
    warn "Coverage enabled"
fi

cd "${ROOT}"

CC="${QC_CC}" CXX="${QC_CXX}" cmake --preset "${PRESET}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBUILD_TESTS="${BUILD_TESTS}" \
    -DENABLE_ASAN="${ENABLE_ASAN}" \
    -DENABLE_TSAN="${ENABLE_TSAN}" \
    -DENABLE_UBSAN="${ENABLE_UBSAN}" \
    -DENABLE_COVERAGE="${ENABLE_COVERAGE}"

cmake --build --preset "${PRESET}" --parallel "${JOBS}"

success "Build complete"
echo "  Binary: ${BUILD_DIR}/quantclaw"
if [[ "${BUILD_TESTS}" == "ON" ]]; then
    echo "  Tests:  ${BUILD_DIR}/quantclaw_tests"
fi
