#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()    { echo -e "${CYAN}[install]${NC} $*"; }
success() { echo -e "${GREEN}[install]${NC} $*"; }
warn()    { echo -e "${YELLOW}[install]${NC} $*"; }
die()     { echo -e "${RED}[install] ERROR:${NC} $*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "${SCRIPT_DIR}/../CMakeLists.txt" ]]; then
  ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
else
  ROOT="${SCRIPT_DIR}"
fi

BUILD_PRESET="gcc16-ninja"
BUILD_DIR="${ROOT}/build-cmake43"
PREBUILT_BINARY="${SCRIPT_DIR}/bin/quantclaw"

[[ "${EUID}" -eq 0 ]] || die "Please run as root (sudo)"

if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  OS="${ID}"
else
  die "Cannot detect OS (missing /etc/os-release)"
fi

install_deps() {
  info "Installing dependencies for ${OS}..."
  case "${OS}" in
    ubuntu|debian)
      apt-get update
      apt-get install -y \
        build-essential \
        ninja-build \
        cmake \
        git \
        curl \
        pkg-config \
        zip \
        unzip \
        tar \
        libssl-dev \
        libcurl4-openssl-dev \
        zlib1g-dev
      if apt-cache show gcc-16 >/dev/null 2>&1 && apt-cache show g++-16 >/dev/null 2>&1; then
        apt-get install -y gcc-16 g++-16
      else
        warn "gcc-16/g++-16 packages unavailable; using system gcc/g++"
      fi
      ;;
    fedora|centos|rhel)
      dnf install -y \
        gcc gcc-c++ \
        ninja-build \
        cmake \
        git \
        curl \
        pkgconf-pkg-config \
        zip \
        unzip \
        tar \
        openssl-devel \
        libcurl-devel \
        zlib-devel
      ;;
    arch|manjaro)
      pacman -S --noconfirm \
        base-devel \
        ninja \
        cmake \
        git \
        curl \
        pkgconf \
        zip \
        unzip \
        openssl \
        zlib
      ;;
    *)
      die "Unsupported OS: ${OS}"
      ;;
  esac
}

bootstrap_vcpkg() {
  local vcpkg_root="${ROOT}/.vcpkg"
  if [[ ! -d "${vcpkg_root}" ]]; then
    die "Missing vcpkg directory: ${vcpkg_root}"
  fi
  if [[ ! -x "${vcpkg_root}/vcpkg" ]]; then
    info "Bootstrapping vcpkg..."
    "${vcpkg_root}/bootstrap-vcpkg.sh" -disableMetrics
  fi
}

pick_compiler() {
  QC_CC="gcc"
  QC_CXX="g++"
  if command -v gcc-16 >/dev/null 2>&1 && command -v g++-16 >/dev/null 2>&1; then
    QC_CC="gcc-16"
    QC_CXX="g++-16"
  fi

  command -v "${QC_CC}" >/dev/null 2>&1 || die "${QC_CC} not found"
  command -v "${QC_CXX}" >/dev/null 2>&1 || die "${QC_CXX} not found"

  local gcc_version
  local gcc_major
  gcc_version="$("${QC_CXX}" -dumpfullversion 2>/dev/null || "${QC_CXX}" -dumpversion 2>/dev/null || true)"
  gcc_major="${gcc_version%%.*}"
  [[ -n "${gcc_major}" ]] || die "Failed to detect GCC version"

  if (( gcc_major < 15 )); then
    die "GCC 15+ is required for this C++23 modules fork (detected ${QC_CXX} ${gcc_version})"
  fi

  info "Using compiler: ${QC_CXX} (${gcc_version})"
}

install_from_prebuilt() {
  info "Installing prebuilt binary from ${PREBUILT_BINARY}"
  install -m 0755 "${PREBUILT_BINARY}" /usr/local/bin/quantclaw
}

build_and_install_from_source() {
  install_deps
  bootstrap_vcpkg
  pick_compiler

  info "Configuring with preset ${BUILD_PRESET}"
  cd "${ROOT}"
  CC="${QC_CC}" CXX="${QC_CXX}" cmake --preset "${BUILD_PRESET}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF

  info "Building QuantClaw"
  cmake --build --preset "${BUILD_PRESET}" --parallel "$(nproc 2>/dev/null || echo 4)"

  info "Installing binary to /usr/local/bin"
  cmake --install "${BUILD_DIR}" --prefix /usr/local
}

setup_workspace() {
  local target_user_home="${HOME}"
  if [[ -n "${SUDO_USER:-}" ]]; then
    target_user_home="$(eval echo "~${SUDO_USER}")"
  fi

  info "Creating workspace in ${target_user_home}/.quantclaw"
  mkdir -p "${target_user_home}/.quantclaw/agents/main/workspace"
  mkdir -p "${target_user_home}/.quantclaw/agents/main/sessions"
  mkdir -p "${target_user_home}/.quantclaw/logs"

  if [[ ! -f "${target_user_home}/.quantclaw/quantclaw.json" ]]; then
    local config_template="${ROOT}/config.example.json"
    if [[ -f "${config_template}" ]]; then
      info "Installing config template"
      install -m 0600 "${config_template}" "${target_user_home}/.quantclaw/quantclaw.json"
    else
      warn "config.example.json not found; skipping config bootstrap"
    fi
  fi

  if [[ -f "${SCRIPT_DIR}/env.example.txt" ]]; then
    install -m 0644 "${SCRIPT_DIR}/env.example.txt" "${target_user_home}/.quantclaw/env.example.txt"
  fi

  if [[ -n "${SUDO_USER:-}" ]]; then
    chown -R "${SUDO_USER}:$(id -gn "${SUDO_USER}")" "${target_user_home}/.quantclaw"
  fi
}

info "Installing QuantClaw..."
if [[ -x "${PREBUILT_BINARY}" ]]; then
  install_from_prebuilt
else
  build_and_install_from_source
fi

command -v quantclaw >/dev/null 2>&1 || die "quantclaw not found in PATH after install"
setup_workspace

echo ""
success "QuantClaw installed successfully"
echo ""
echo "Next steps:"
echo "1. Edit ~/.quantclaw/quantclaw.json with your provider API keys"
echo "2. Run onboarding (optional): quantclaw onboard --quick"
echo "3. Start gateway: quantclaw gateway"
echo "4. Check status: quantclaw status"
echo "5. Open dashboard: quantclaw dashboard"
