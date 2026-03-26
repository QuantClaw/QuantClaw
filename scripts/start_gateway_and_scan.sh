#!/usr/bin/env bash
set -euo pipefail

# Starts QuantClaw gateway in background and verifies local ports with nmap + ss.
# Usage:
#   scripts/start_gateway_and_scan.sh [port]
# Example:
#   scripts/start_gateway_and_scan.sh 18800

PORT="${1:-18800}"
BIND="127.0.0.1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -x "$ROOT_DIR/build-vcpkg/quantclaw" ]]; then
  BIN="$ROOT_DIR/build-vcpkg/quantclaw"
elif [[ -x "$ROOT_DIR/build/quantclaw" ]]; then
  BIN="$ROOT_DIR/build/quantclaw"
else
  echo "[error] quantclaw binary not found. Build first (e.g. build-vcpkg/quantclaw)." >&2
  exit 1
fi

if ! command -v nmap >/dev/null 2>&1; then
  echo "[error] nmap is not installed." >&2
  exit 1
fi

LOG_DIR="$ROOT_DIR/.logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/gateway-${PORT}.log"
PID_FILE="$ROOT_DIR/.gateway-${PORT}.pid"

# Stop stale pid if file exists but process is gone.
if [[ -f "$PID_FILE" ]]; then
  OLD_PID="$(cat "$PID_FILE" || true)"
  if [[ -n "${OLD_PID:-}" ]] && kill -0 "$OLD_PID" 2>/dev/null; then
    echo "[info] gateway already running (pid=$OLD_PID, port=$PORT)"
  else
    rm -f "$PID_FILE"
  fi
fi

# Start gateway if not already listening.
if ss -ltn "sport = :$PORT" | grep -q LISTEN; then
  echo "[info] port $PORT already has a listener; skipping start"
else
  echo "[info] starting gateway on $BIND:$PORT"
  nohup "$BIN" gateway run --bind "$BIND" --port "$PORT" >"$LOG_FILE" 2>&1 &
  GW_PID=$!
  echo "$GW_PID" > "$PID_FILE"

  # Wait up to 10s for listener.
  READY=0
  for _ in {1..20}; do
    if ss -ltn "sport = :$PORT" | grep -q LISTEN; then
      READY=1
      break
    fi
    sleep 0.5
  done

  if [[ "$READY" -ne 1 ]]; then
    echo "[error] gateway did not start listening on port $PORT" >&2
    echo "[hint] recent log lines:" >&2
    tail -n 60 "$LOG_FILE" >&2 || true
    exit 1
  fi
  echo "[ok] gateway started (pid=$GW_PID)"
fi

echo
echo "== nmap scan =="
nmap -Pn -p 18800,18801,18900 127.0.0.1

echo
echo "== ss listeners =="
ss -ltnp | awk 'NR==1 || /:18800|:18801|:18900/'

echo
echo "[done] gateway log: $LOG_FILE"
echo "[done] pid file: $PID_FILE"
echo "[tip] stop with: kill \"$(cat "$PID_FILE")\" && rm -f "$PID_FILE""
