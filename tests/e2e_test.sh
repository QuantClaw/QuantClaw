#!/bin/bash
# QuantClaw CLI End-to-End Test Script
# Starts a real gateway process and exercises CLI commands against it.
set -euo pipefail

PASS=0
FAIL=0
PORT=18800
BINARY="${BINARY:-./build/quantclaw}"
GATEWAY_PID=""

# ---------- helpers ----------

cleanup() {
    if [ -n "$GATEWAY_PID" ] && kill -0 "$GATEWAY_PID" 2>/dev/null; then
        kill -TERM "$GATEWAY_PID" 2>/dev/null || true
        wait "$GATEWAY_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

pass() {
    echo "  [PASS] $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "  [FAIL] $1: $2"
    FAIL=$((FAIL + 1))
}

# Run a CLI command and extract only JSON output (spdlog writes [info]/[warn] to stdout)
run_cli() {
    "$BINARY" "$@" 2>/dev/null | grep -v '^\[' || true
}

check_json_field() {
    local json="$1" field="$2" expected="$3" label="$4"
    local actual
    actual=$(echo "$json" | jq -r "$field" 2>/dev/null)
    if [ "$actual" = "$expected" ]; then
        pass "$label"
    else
        fail "$label" "expected '$expected', got '$actual'"
    fi
}

wait_for_gateway() {
    local max_wait=30
    local waited=0
    while [ $waited -lt $max_wait ]; do
        if run_cli health --json | jq -e '.status == "ok"' >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

# ---------- pre-checks ----------

echo "=== QuantClaw CLI E2E Tests ==="
echo ""

if [ ! -x "$BINARY" ]; then
    echo "ERROR: binary not found at $BINARY"
    exit 1
fi

# Write minimal config (auth mode=none so CLI doesn't need a token)
mkdir -p "$HOME/.quantclaw"
cat > "$HOME/.quantclaw/quantclaw.json" <<'EOFCFG'
{
    "agent": {
        "model": "test-model",
        "maxIterations": 15,
        "temperature": 0.7,
        "maxTokens": 4096
    },
    "gateway": {
        "port": 18800,
        "bind": "loopback",
        "auth": {
            "mode": "none",
            "token": ""
        }
    },
    "providers": {},
    "tools": {},
    "mcp": {
        "servers": []
    }
}
EOFCFG

# ---------- 1. Start gateway ----------

echo "--- Starting gateway on port $PORT ---"
"$BINARY" gateway --port "$PORT" >/dev/null 2>&1 &
GATEWAY_PID=$!
echo "  Gateway PID: $GATEWAY_PID"

if ! wait_for_gateway; then
    echo "ERROR: gateway did not become ready within 30s"
    exit 1
fi
echo "  Gateway is ready."
echo ""

# ---------- 2. Health ----------

echo "--- Test: health ---"
HEALTH=$(run_cli health --json)
check_json_field "$HEALTH" '.status' 'ok' "health.status"
if echo "$HEALTH" | jq -e '.version' >/dev/null 2>&1; then
    pass "health.version present"
else
    fail "health.version present" "field missing"
fi

# ---------- 3. Status ----------

echo "--- Test: status ---"
STATUS=$(run_cli status --json)
check_json_field "$STATUS" '.running' 'true' "status.running"
if echo "$STATUS" | jq -e '.connections' >/dev/null 2>&1; then
    pass "status.connections present"
else
    fail "status.connections present" "field missing"
fi
if echo "$STATUS" | jq -e '.uptime >= 0' >/dev/null 2>&1; then
    pass "status.uptime >= 0"
else
    fail "status.uptime >= 0" "field missing or negative"
fi

# ---------- 4. Config get ----------

echo "--- Test: config get ---"
MODEL=$(run_cli config get agent.model)
if [ -n "$MODEL" ]; then
    pass "config get agent.model returned value"
else
    fail "config get agent.model" "empty response"
fi

# ---------- 5. Sessions list ----------

echo "--- Test: sessions list ---"
SESSIONS=$(run_cli sessions list --json)
if echo "$SESSIONS" | jq -e 'type == "array" or .sessions' >/dev/null 2>&1; then
    pass "sessions list returns array/object"
else
    fail "sessions list" "not an array"
fi

# ---------- 6. Graceful shutdown ----------

echo "--- Test: graceful shutdown ---"
kill -TERM "$GATEWAY_PID" 2>/dev/null || true
set +e
wait "$GATEWAY_PID" 2>/dev/null
EXIT_CODE=$?
set -e
GATEWAY_PID=""

if [ "$EXIT_CODE" -eq 0 ] || [ "$EXIT_CODE" -eq 143 ]; then
    pass "gateway exited cleanly (code=$EXIT_CODE)"
else
    fail "gateway exit" "unexpected exit code $EXIT_CODE"
fi

# ---------- Summary ----------

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
