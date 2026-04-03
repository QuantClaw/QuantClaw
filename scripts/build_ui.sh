#!/bin/bash
set -e

UI_DIR="${HOME}/.quantclaw/ui"
# Resolve the repo root relative to this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Building QuantClaw UI from local source..."

cd "${REPO_ROOT}/ui"

echo "Installing dependencies..."
if command -v pnpm &> /dev/null; then
    pnpm install
elif command -v npm &> /dev/null; then
    npm install
else
    echo "Error: npm or pnpm is required"
    exit 1
fi

echo "Building..."
if command -v pnpm &> /dev/null; then
    pnpm build
else
    npm run build
fi

echo "Installing to $UI_DIR..."
rm -rf "$UI_DIR"
mkdir -p "$UI_DIR"
cp -r dist/* "$UI_DIR/"

# Inject gateway config into index.html
tmp_index_html="$(mktemp "${UI_DIR}/index.html.XXXXXX")"
sed 's|<head>|<head><script>window.__QUANTCLAW_GATEWAY_WS_PORT=18800;</script>|' "$UI_DIR/index.html" > "$tmp_index_html"
mv "$tmp_index_html" "$UI_DIR/index.html"

echo "Done. Dashboard UI installed at $UI_DIR"
