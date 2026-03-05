#!/usr/bin/env bash
# QuantClaw release build script
# Produces a versioned tarball + SHA256 checksum, similar to pikiwidb's
# ci/release-build.sh pattern.
#
# Usage:
#   ./scripts/release.sh [VERSION]
#
# Examples:
#   ./scripts/release.sh                  # reads scripts/DOCKER_VERSION
#   ./scripts/release.sh 0.3.0-alpha      # explicit version
#
# Output:
#   dist/quantclaw-<VERSION>-<os>-<arch>.tar.gz
#   dist/quantclaw-<VERSION>-<os>-<arch>.tar.gz.sha256

set -euo pipefail

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

YELLOW='\033[1;33m'

info() { echo -e "${CYAN}[release]${NC} $*"; }
success() { echo -e "${GREEN}[release]${NC} $*"; }
warn() { echo -e "${YELLOW}[release]${NC} $*"; }
die()  { echo -e "${RED}[release] ERROR:${NC} $*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Version ───────────────────────────────────────────────────────────────────
VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
    VERSION_FILE="$SCRIPT_DIR/DOCKER_VERSION"
    [[ -f "$VERSION_FILE" ]] || die "No version given and $VERSION_FILE not found."
    VERSION="$(cat "$VERSION_FILE" | tr -d '[:space:]')"
fi
[[ -n "$VERSION" ]] || die "Empty version string."

# ── Platform detection ────────────────────────────────────────────────────────
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64)  ARCH=amd64 ;;
    aarch64) ARCH=arm64 ;;
    arm*)    ARCH=arm   ;;
esac

ARTIFACT_NAME="quantclaw-${VERSION}-${OS}-${ARCH}"
DIST_DIR="$ROOT/dist"
mkdir -p "$DIST_DIR"

# ── Build ─────────────────────────────────────────────────────────────────────
BUILD_DIR="$ROOT/build"
info "Building $ARTIFACT_NAME..."

# Use build.sh if available, otherwise fall back to raw cmake
if [[ -x "$SCRIPT_DIR/build.sh" ]]; then
    "$SCRIPT_DIR/build.sh"
else
    CPU_CORES=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
    cmake -B "$BUILD_DIR" -S "$ROOT" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_TESTS=OFF
    cmake --build "$BUILD_DIR" --parallel "$CPU_CORES"
fi

BINARY="$BUILD_DIR/quantclaw"
[[ -f "$BINARY" ]] || die "Binary not found: $BINARY"

# ── Assemble staging directory ────────────────────────────────────────────────
STAGE="$DIST_DIR/$ARTIFACT_NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin"

# Strip binary to reduce size
cp "$BINARY" "$STAGE/bin/quantclaw"
strip "$STAGE/bin/quantclaw" 2>/dev/null || true

# Include sidecar dist + pruned node_modules (if built)
SIDECAR_DIST="$ROOT/sidecar/dist"
SIDECAR_MODS="$ROOT/sidecar/node_modules"
if [[ -d "$SIDECAR_DIST" ]]; then
    mkdir -p "$STAGE/sidecar"
    cp -r "$SIDECAR_DIST" "$STAGE/sidecar/dist"
    [[ -d "$SIDECAR_MODS" ]] && cp -r "$SIDECAR_MODS" "$STAGE/sidecar/node_modules"
    info "Sidecar included in release."
fi

# Include skills/
SKILLS_DIR="$ROOT/skills"
if [[ -d "$SKILLS_DIR" ]]; then
    cp -r "$SKILLS_DIR" "$STAGE/skills"
fi

# Include scripts/install.sh renamed for convenience
cp "$SCRIPT_DIR/install.sh" "$STAGE/install.sh"
chmod +x "$STAGE/install.sh"

# Write version file
echo "$VERSION" > "$STAGE/VERSION"

# ── Tarball ───────────────────────────────────────────────────────────────────
TARBALL="$DIST_DIR/${ARTIFACT_NAME}.tar.gz"
info "Creating tarball: $(basename "$TARBALL")"
tar -czf "$TARBALL" -C "$DIST_DIR" "$ARTIFACT_NAME"

# ── SHA256 checksum ───────────────────────────────────────────────────────────
SHA_FILE="${TARBALL}.sha256"
if command -v sha256sum &>/dev/null; then
    sha256sum "$TARBALL" | awk '{print $1}' > "$SHA_FILE"
elif command -v shasum &>/dev/null; then
    shasum -a 256 "$TARBALL" | awk '{print $1}' > "$SHA_FILE"
else
    warn "sha256sum/shasum not found — skipping checksum."
fi

# ── Clean up staging dir ──────────────────────────────────────────────────────
rm -rf "$STAGE"

# ── Summary ───────────────────────────────────────────────────────────────────
SIZE=$(du -sh "$TARBALL" | cut -f1)
success "Release artifact:"
echo "  $TARBALL  ($SIZE)"
if [[ -f "$SHA_FILE" ]]; then
    echo "  SHA256: $(cat "$SHA_FILE")"
fi
