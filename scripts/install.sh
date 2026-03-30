#!/usr/bin/env bash
set -euo pipefail

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

info()    { echo -e "${CYAN}[install]${NC} $*"; }
success() { echo -e "${GREEN}[install]${NC} $*"; }
warn()    { echo -e "${YELLOW}[install]${NC} $*"; }
die()     { echo -e "${RED}[install] ERROR:${NC} $*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PLATFORM=""
INSTALL_MODE=""
SOURCE_BINARY="${QUANTCLAW_BUILD_BINARY:-}"
SKIP_DEPS="${QUANTCLAW_INSTALL_SKIP_DEPS:-0}"
SKIP_SERVICE=0
TEST_MODE="${QUANTCLAW_INSTALL_TEST_MODE:-0}"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/install.sh [options]

Options:
  --user            Install to ~/.quantclaw/bin (default on macOS)
  --system          Install to /usr/local/bin (default on Linux)
  --binary PATH     Use an existing quantclaw binary instead of building
  --skip-deps       Skip dependency installation
  --skip-service    Do not install the background service definition
  -h, --help        Show this message
EOF
}

detect_platform() {
  case "$(uname -s)" in
    Darwin) PLATFORM="macos" ;;
    Linux) PLATFORM="linux" ;;
    *) die "Unsupported OS: $(uname -s)" ;;
  esac

  if [[ -z "$INSTALL_MODE" ]]; then
    if [[ "$PLATFORM" == "macos" ]]; then
      INSTALL_MODE="user"
    else
      INSTALL_MODE="system"
    fi
  fi
}

ensure_brew() {
  command -v brew >/dev/null 2>&1 || die "Homebrew is required on macOS"
}

ensure_brew_package() {
  local pkg="$1"
  brew list --versions "$pkg" >/dev/null 2>&1 && return 0
  info "Installing ${pkg} via Homebrew..."
  brew install "$pkg"
}

lookup_user_home() {
  local user="$1"
  if [[ -z "$user" || "$user" == "root" ]]; then
    return 1
  fi

  if [[ "$PLATFORM" == "linux" ]]; then
    getent passwd "$user" | cut -d: -f6
  else
    dscl . -read "/Users/${user}" NFSHomeDirectory 2>/dev/null \
      | awk '{print $2}'
  fi
}

install_linux_deps() {
  [[ "$SKIP_DEPS" == "1" ]] && return 0
  if [[ "$INSTALL_MODE" == "user" && $EUID -ne 0 ]]; then
    warn "User-mode install cannot install system packages without root."
    warn "Please install dependencies manually or run with sudo and --user flag."
    return 0
  fi
  [[ -f /etc/os-release ]] || die "Cannot detect Linux distribution"
  . /etc/os-release
  info "Installing Linux build dependencies..."
  case "${ID:-}" in
    ubuntu|debian)
      apt-get update
      apt-get install -y \
        build-essential cmake ninja-build git \
        libssl-dev libcurl4-openssl-dev nlohmann-json3-dev \
        libspdlog-dev zlib1g-dev
      ;;
    fedora|centos|rhel)
      dnf install -y \
        gcc gcc-c++ cmake ninja-build git \
        openssl-devel libcurl-devel nlohmann_json-devel \
        spdlog-devel zlib-devel
      ;;
    arch|manjaro)
      pacman -S --noconfirm \
        base-devel cmake ninja git \
        openssl curl nlohmann-json spdlog zlib
      ;;
    *)
      die "Unsupported Linux distribution: ${ID:-unknown}"
      ;;
  esac
}

install_macos_deps() {
  [[ "$SKIP_DEPS" == "1" ]] && return 0
  ensure_brew
  info "Installing macOS build dependencies via Homebrew..."
  for pkg in cmake ninja pkg-config git spdlog openssl@3 curl; do
    ensure_brew_package "$pkg"
  done
  ensure_brew_package node
}

build_binary() {
  if [[ -n "$SOURCE_BINARY" ]]; then
    [[ -x "$SOURCE_BINARY" ]] || die "Binary not found or not executable: $SOURCE_BINARY"
    return 0
  fi

  info "Building QuantClaw from source..."
  bash "$ROOT/scripts/build.sh"
  SOURCE_BINARY="$ROOT/build/quantclaw"
  [[ -x "$SOURCE_BINARY" ]] || die "Build completed but binary not found: $SOURCE_BINARY"
}

resolve_target_home() {
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
    local sudo_home=""
    sudo_home="$(lookup_user_home "${SUDO_USER}")" || true
    if [[ -n "$sudo_home" ]]; then
      echo "$sudo_home"
      return 0
    fi
  fi
  echo "${HOME}"
}

run_for_target_user() {
  local target_home="$1"
  shift

  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
    sudo -u "$SUDO_USER" -H env HOME="$target_home" "$@"
    return
  fi

  HOME="$target_home" "$@"
}

install_binary() {
  local target_home="$1"
  local target_dir=""
  if [[ "$INSTALL_MODE" == "user" ]]; then
    target_dir="${target_home}/.quantclaw/bin"
  else
    target_dir="${QUANTCLAW_INSTALL_PREFIX:-/usr/local/bin}"
  fi

  run_for_target_user "$target_home" mkdir -p "$target_dir"
  run_for_target_user "$target_home" cp "$SOURCE_BINARY" "${target_dir}/quantclaw"
  run_for_target_user "$target_home" chmod +x "${target_dir}/quantclaw"
  echo "${target_dir}/quantclaw"
}

run_onboard() {
  local target_home="$1"
  local target_bin="$2"
  info "Creating workspace and config..."
  run_for_target_user "$target_home" "$target_bin" onboard --quick >/dev/null
}

install_service_definition() {
  local target_home="$1"
  local target_bin="$2"
  [[ "$SKIP_SERVICE" -eq 1 ]] && return 0
  info "Installing background service definition..."
  if ! run_for_target_user "$target_home" "$target_bin" gateway install \
      >/dev/null; then
    die "Failed to install background service definition"
  fi
}

print_next_steps() {
  local target_home="$1"
  local target_bin="$2"

  echo
  success "QuantClaw installed successfully"
  echo
  if [[ "$INSTALL_MODE" == "user" ]]; then
    echo "Add to PATH if needed:"
    echo "  export PATH=\"${target_home}/.quantclaw/bin:\$PATH\""
    echo
  fi
  echo "Next steps:"
  echo "  1. Edit ${target_home}/.quantclaw/quantclaw.json with your API keys"
  echo "  2. Start in foreground: ${target_bin} gateway run"
  echo "  3. Or start via service manager: ${target_bin} gateway start"
  echo "  4. Check status: ${target_bin} status"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      INSTALL_MODE="user"
      ;;
    --system)
      INSTALL_MODE="system"
      ;;
    --binary)
      [[ $# -ge 2 ]] || die "--binary requires a path"
      SOURCE_BINARY="$2"
      shift
      ;;
    --skip-deps)
      SKIP_DEPS=1
      ;;
    --skip-service)
      SKIP_SERVICE=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown option: $1"
      ;;
  esac
  shift
done

detect_platform

if [[ "$INSTALL_MODE" == "system" && "$TEST_MODE" != "1" && "$EUID" -ne 0 ]]; then
  die "--system install requires sudo/root"
fi

TARGET_HOME="$(resolve_target_home)"

if [[ "$PLATFORM" == "macos" ]]; then
  install_macos_deps
else
  install_linux_deps
fi

build_binary
TARGET_BIN="$(install_binary "$TARGET_HOME")"
run_onboard "$TARGET_HOME" "$TARGET_BIN"
install_service_definition "$TARGET_HOME" "$TARGET_BIN"
print_next_steps "$TARGET_HOME" "$TARGET_BIN"
