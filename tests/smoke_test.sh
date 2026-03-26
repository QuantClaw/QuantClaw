#!/bin/bash
# QuantClaw Smoke Test Suite
# Tests gateway lifecycle, WebSocket RPC, HTTP API, and concurrent connections.
# Runs without an API key (agent tests are skipped); set OPENAI_API_KEY to enable them.
#
# Usage: bash tests/smoke_test.sh
set -uo pipefail

# ---------- Configuration ----------

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="${BINARY:-${REPO_ROOT}/build/quantclaw}"
WS_RPC="${REPO_ROOT}/scripts/smoke-tests/ws-rpc.js"
WS_CONCURRENT="${REPO_ROOT}/scripts/smoke-tests/ws-concurrent.js"
LOG_DIR="/tmp/quantclaw-smoke-ci"
SMOKE_HOME="${LOG_DIR}/home"
WS_PORT=18850
HTTP_PORT=18851
TOKEN="smoke-test-token-$(date +%s)"
GATEWAY_PID=""
PASS=0
FAIL=0
SKIP=0
TOTAL=0

# ---------- Helpers ----------

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
    TOTAL=$((TOTAL + 1))
}

fail() {
    echo "  [FAIL] $1: $2"
    FAIL=$((FAIL + 1))
    TOTAL=$((TOTAL + 1))
}

skip() {
    echo "  [SKIP] $1"
    SKIP=$((SKIP + 1))
    TOTAL=$((TOTAL + 1))
}

# Run a curl request and capture body
# Usage: http_get <url> [extra-curl-args...]
http_get() {
    local url="$1"; shift
    curl -sf --max-time 10 --noproxy '*' -H "Authorization: Bearer ${TOKEN}" "$@" "$url" 2>/dev/null
}

http_post() {
    local url="$1"; shift
    curl -sf --max-time 10 --noproxy '*' -X POST -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" "$@" "$url" 2>/dev/null
}

# Run a WS RPC call via the Node.js helper
# Usage: ws_rpc <test-name> [method] [params-json]
ws_rpc() {
    node "$WS_RPC" "ws://127.0.0.1:${WS_PORT}" "$TOKEN" "$@" 2>/dev/null
}

wait_for_gateway() {
    local max_wait=30 waited=0
    while [ $waited -lt $max_wait ]; do
        if http_get "http://127.0.0.1:${HTTP_PORT}/api/health" | jq -e '.status == "ok"' >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

# ---------- Pre-checks ----------

echo "=== QuantClaw Smoke Tests ==="
echo ""

if [ ! -x "$BINARY" ]; then
    echo "ERROR: binary not found at $BINARY"
    echo "  Run: cd build && cmake --build . -j\$(nproc)"
    exit 1
fi

if ! command -v node >/dev/null 2>&1; then
    echo "ERROR: node not found in PATH"
    exit 1
fi

if [ ! -f "${REPO_ROOT}/scripts/smoke-tests/node_modules/ws/index.js" ]; then
    echo "--- Installing ws dependency ---"
    (cd "${REPO_ROOT}/scripts/smoke-tests" && npm install --silent)
fi

if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR: jq not found in PATH"
    exit 1
fi

# ---------- Setup ----------

mkdir -p "$LOG_DIR" "${SMOKE_HOME}/.quantclaw"

cat > "${SMOKE_HOME}/.quantclaw/quantclaw.json" <<EOFCFG
{
    "agent": {
        "model": "openai/test-model",
        "maxIterations": 5,
        "temperature": 0.7,
        "maxTokens": 4096
    },
    "gateway": {
        "port": ${WS_PORT},
        "bind": "loopback",
        "auth": {
            "mode": "token",
            "token": "${TOKEN}"
        },
        "controlUi": {
            "enabled": true,
            "port": ${HTTP_PORT}
        }
    },
    "providers": {},
    "tools": {},
    "mcp": { "servers": [] },
    "plugins": {}
}
EOFCFG

# Start gateway
echo "--- Starting gateway (WS=$WS_PORT, HTTP=$HTTP_PORT) ---"
HOME="$SMOKE_HOME" "$BINARY" gateway --port "$WS_PORT" >"${LOG_DIR}/gateway.log" 2>&1 &
GATEWAY_PID=$!
echo "  PID: $GATEWAY_PID"

if ! wait_for_gateway; then
    echo "ERROR: gateway did not become ready within 30s"
    echo "  Gateway log:"
    tail -20 "${LOG_DIR}/gateway.log" 2>/dev/null || true
    exit 1
fi
echo "  Gateway is ready."
echo ""

# ==========================================================
# Phase 1 — Lifecycle
# ==========================================================

echo "--- Phase 1: Lifecycle ---"

# S1.1 HTTP health
HEALTH=$(http_get "http://127.0.0.1:${HTTP_PORT}/api/health")
if echo "$HEALTH" | jq -e '.status == "ok"' >/dev/null 2>&1; then
    pass "S1.1 HTTP /api/health"
else
    fail "S1.1 HTTP /api/health" "got: $HEALTH"
fi

# S1.2 HTTP status
STATUS=$(http_get "http://127.0.0.1:${HTTP_PORT}/api/status")
if echo "$STATUS" | jq -e '.running == true' >/dev/null 2>&1; then
    pass "S1.2 HTTP /api/status"
else
    fail "S1.2 HTTP /api/status" "got: $STATUS"
fi

# S1.3 WS connect.hello
HELLO=$(ws_rpc hello)
if [ $? -eq 0 ] && echo "$HELLO" | jq -e '.protocol' >/dev/null 2>&1; then
    pass "S1.3 WS connect.hello"
else
    fail "S1.3 WS connect.hello" "got: $HELLO"
fi

# S1.4 WS auth reject (bad token)
REJECT_OUT=$(node "$WS_RPC" "ws://127.0.0.1:${WS_PORT}" "wrong-token" auth-reject 2>/dev/null)
if [ $? -eq 0 ]; then
    pass "S1.4 WS auth reject"
else
    fail "S1.4 WS auth reject" "expected rejection, got success"
fi

echo ""

# ==========================================================
# Phase 2 — Config RPCs
# ==========================================================

echo "--- Phase 2: Config RPCs ---"

# S2.1 config.get (full)
CFG=$(ws_rpc rpc config.get)
if [ $? -eq 0 ] && echo "$CFG" | jq -e '.config.agent' >/dev/null 2>&1; then
    pass "S2.1 WS config.get (full)"
else
    fail "S2.1 WS config.get (full)" "got: $CFG"
fi

# S2.2 config.get (dot-path)
MODEL=$(ws_rpc rpc config.get '{"path":"agent.model"}')
if [ $? -eq 0 ] && [ -n "$MODEL" ]; then
    pass "S2.2 WS config.get (dot-path)"
else
    fail "S2.2 WS config.get (dot-path)" "got: $MODEL"
fi

# S2.3 config.set + verify
ws_rpc rpc config.set '{"path":"agent.temperature","value":0.5}' >/dev/null 2>&1
TEMP=$(ws_rpc rpc config.get '{"path":"agent.temperature"}')
if echo "$TEMP" | grep -q "0.5"; then
    pass "S2.3 WS config.set + verify"
else
    fail "S2.3 WS config.set + verify" "got: $TEMP"
fi

# S2.4 config.reload
RELOAD=$(ws_rpc rpc config.reload)
if [ $? -eq 0 ]; then
    pass "S2.4 WS config.reload"
else
    fail "S2.4 WS config.reload" "got: $RELOAD"
fi

# S2.5 HTTP /api/config
HCFG=$(http_get "http://127.0.0.1:${HTTP_PORT}/api/config")
if echo "$HCFG" | jq -e '.agent' >/dev/null 2>&1; then
    pass "S2.5 HTTP /api/config"
else
    fail "S2.5 HTTP /api/config" "got: $HCFG"
fi

echo ""

# ==========================================================
# Phase 3 — Session RPCs
# ==========================================================

echo "--- Phase 3: Session RPCs ---"

# S3.1 sessions.list
SLIST=$(ws_rpc rpc sessions.list)
if [ $? -eq 0 ] && echo "$SLIST" | jq -e 'type == "array" or .sessions' >/dev/null 2>&1; then
    pass "S3.1 WS sessions.list"
else
    fail "S3.1 WS sessions.list" "got: $SLIST"
fi

# S3.2 sessions.history (empty session — should not error)
SHIST=$(ws_rpc rpc sessions.history '{"sessionKey":"smoke:test"}')
if [ $? -eq 0 ]; then
    pass "S3.2 WS sessions.history"
else
    fail "S3.2 WS sessions.history" "got: $SHIST"
fi

# S3.3 sessions.reset
SRESET=$(ws_rpc rpc sessions.reset '{"sessionKey":"smoke:test"}')
if [ $? -eq 0 ]; then
    pass "S3.3 WS sessions.reset"
else
    fail "S3.3 WS sessions.reset" "got: $SRESET"
fi

# S3.4 sessions.delete
SDEL=$(ws_rpc rpc sessions.delete '{"sessionKey":"smoke:test"}')
if [ $? -eq 0 ]; then
    pass "S3.4 WS sessions.delete"
else
    fail "S3.4 WS sessions.delete" "got: $SDEL"
fi

# S3.5 HTTP /api/sessions
HSESS=$(http_get "http://127.0.0.1:${HTTP_PORT}/api/sessions")
if echo "$HSESS" | jq -e 'type == "array" or .sessions' >/dev/null 2>&1; then
    pass "S3.5 HTTP /api/sessions"
else
    fail "S3.5 HTTP /api/sessions" "got: $HSESS"
fi

echo ""

# ==========================================================
# Phase 4 — Agent (requires API key)
# ==========================================================

echo "--- Phase 4: Agent ---"

if [ -n "${OPENAI_API_KEY:-}" ]; then
    # S4.1 WS agent.request
    AREQ=$(ws_rpc rpc agent.request '{"message":"Say hello in one word","sessionKey":"smoke:agent"}')
    if [ $? -eq 0 ]; then
        pass "S4.1 WS agent.request"
    else
        fail "S4.1 WS agent.request" "got: $AREQ"
    fi

    # S4.2 HTTP /api/agent/request
    HAREQ=$(http_post "http://127.0.0.1:${HTTP_PORT}/api/agent/request" \
        --max-time 30 \
        -d '{"message":"Say hi in one word","sessionKey":"smoke:http-agent"}')
    if [ $? -eq 0 ] && [ -n "$HAREQ" ]; then
        pass "S4.2 HTTP /api/agent/request"
    else
        fail "S4.2 HTTP /api/agent/request" "got: $HAREQ"
    fi

    # S4.3 HTTP /api/agent/stream (SSE — just check it connects)
    STREAM=$(curl -sf -N --max-time 15 --noproxy '*' \
        -H "Authorization: Bearer ${TOKEN}" \
        -H "Content-Type: application/json" \
        -d '{"message":"Say hey in one word","sessionKey":"smoke:stream"}' \
        "http://127.0.0.1:${HTTP_PORT}/api/agent/stream" 2>/dev/null | head -5)
    if [ -n "$STREAM" ]; then
        pass "S4.3 HTTP /api/agent/stream (SSE)"
    else
        fail "S4.3 HTTP /api/agent/stream (SSE)" "no data received"
    fi

    # S4.4 HTTP /v1/chat/completions (OpenAI compat)
    CHAT=$(http_post "http://127.0.0.1:${HTTP_PORT}/v1/chat/completions" \
        --max-time 30 \
        -d '{"model":"test-model","messages":[{"role":"user","content":"Say ok"}]}')
    if [ $? -eq 0 ] && echo "$CHAT" | jq -e '.choices' >/dev/null 2>&1; then
        pass "S4.4 HTTP /v1/chat/completions"
    else
        fail "S4.4 HTTP /v1/chat/completions" "got: $CHAT"
    fi
else
    skip "S4.1 WS agent.request (no OPENAI_API_KEY)"
    skip "S4.2 HTTP /api/agent/request (no OPENAI_API_KEY)"
    skip "S4.3 HTTP /api/agent/stream (no OPENAI_API_KEY)"
    skip "S4.4 HTTP /v1/chat/completions (no OPENAI_API_KEY)"
fi

# Always-run tests (no API key needed)

# S4.5 HTTP /v1/models
MODELS=$(http_get "http://127.0.0.1:${HTTP_PORT}/v1/models")
if [ $? -eq 0 ] && [ -n "$MODELS" ]; then
    pass "S4.5 HTTP /v1/models"
else
    fail "S4.5 HTTP /v1/models" "got: $MODELS"
fi

# S4.6 WS tools.catalog
TOOLS=$(ws_rpc rpc tools.catalog)
if [ $? -eq 0 ]; then
    pass "S4.6 WS tools.catalog"
else
    fail "S4.6 WS tools.catalog" "got: $TOOLS"
fi

echo ""

# ==========================================================
# Phase 5 — Plugin RPCs
# ==========================================================

echo "--- Phase 5: Plugin RPCs ---"

# S5.1 plugins.list
PLIST=$(ws_rpc rpc plugins.list)
if [ $? -eq 0 ]; then
    pass "S5.1 WS plugins.list"
else
    fail "S5.1 WS plugins.list" "got: $PLIST"
fi

# S5.2 plugins.tools
PTOOLS=$(ws_rpc rpc plugins.tools)
if [ $? -eq 0 ]; then
    pass "S5.2 WS plugins.tools"
else
    fail "S5.2 WS plugins.tools" "got: $PTOOLS"
fi

# S5.3 plugins.services
PSVC=$(ws_rpc rpc plugins.services)
if [ $? -eq 0 ]; then
    pass "S5.3 WS plugins.services"
else
    fail "S5.3 WS plugins.services" "got: $PSVC"
fi

# S5.4 plugins.providers
PPROV=$(ws_rpc rpc plugins.providers)
if [ $? -eq 0 ]; then
    pass "S5.4 WS plugins.providers"
else
    fail "S5.4 WS plugins.providers" "got: $PPROV"
fi

# S5.5 plugins.commands
PCMD=$(ws_rpc rpc plugins.commands)
if [ $? -eq 0 ]; then
    pass "S5.5 WS plugins.commands"
else
    fail "S5.5 WS plugins.commands" "got: $PCMD"
fi

# S5.6 plugins.gateway (no method — should return empty or error gracefully)
PGWY=$(ws_rpc rpc plugins.gateway '{"method":"nonexistent"}' 2>&1)
# Just check it didn't crash the gateway
if http_get "http://127.0.0.1:${HTTP_PORT}/api/health" | jq -e '.status == "ok"' >/dev/null 2>&1; then
    pass "S5.6 WS plugins.gateway (no crash)"
else
    fail "S5.6 WS plugins.gateway" "gateway unhealthy after call"
fi

# S5.7 HTTP /api/plugins (404 expected when no plugins configured)
HPLUGINS_CODE=$(curl -so /dev/null -w '%{http_code}' --max-time 10 --noproxy '*' \
    -H "Authorization: Bearer ${TOKEN}" "http://127.0.0.1:${HTTP_PORT}/api/plugins" 2>/dev/null)
if [ "$HPLUGINS_CODE" = "200" ] || [ "$HPLUGINS_CODE" = "404" ]; then
    pass "S5.7 HTTP /api/plugins (status=$HPLUGINS_CODE)"
else
    fail "S5.7 HTTP /api/plugins" "unexpected status $HPLUGINS_CODE"
fi

echo ""

# ==========================================================
# Phase 6 — Other RPCs
# ==========================================================

echo "--- Phase 6: Other RPCs ---"

# S6.1 skills.status
SSKILL=$(ws_rpc rpc skills.status)
if [ $? -eq 0 ]; then
    pass "S6.1 WS skills.status"
else
    fail "S6.1 WS skills.status" "got: $SSKILL"
fi

# S6.2 cron.list
SCRON=$(ws_rpc rpc cron.list)
if [ $? -eq 0 ]; then
    pass "S6.2 WS cron.list"
else
    fail "S6.2 WS cron.list" "got: $SCRON"
fi

# S6.3 memory.status
SMEM=$(ws_rpc rpc memory.status)
if [ $? -eq 0 ]; then
    pass "S6.3 WS memory.status"
else
    fail "S6.3 WS memory.status" "got: $SMEM"
fi

# S6.4 queue.status
SQUEUE=$(ws_rpc rpc queue.status)
if [ $? -eq 0 ]; then
    pass "S6.4 WS queue.status"
else
    fail "S6.4 WS queue.status" "got: $SQUEUE"
fi

# S6.5 channels.list
SCHAN=$(ws_rpc rpc channels.list)
if [ $? -eq 0 ]; then
    pass "S6.5 WS channels.list"
else
    fail "S6.5 WS channels.list" "got: $SCHAN"
fi

# S6.6 models.list (OpenClaw compat alias)
SMODELS=$(ws_rpc rpc models.list)
if [ $? -eq 0 ]; then
    pass "S6.6 WS models.list"
else
    fail "S6.6 WS models.list" "got: $SMODELS"
fi

# S6.7 WS gateway.health
GWHEALTH=$(ws_rpc rpc gateway.health)
if [ $? -eq 0 ] && echo "$GWHEALTH" | jq -e '.status == "ok"' >/dev/null 2>&1; then
    pass "S6.7 WS gateway.health"
else
    fail "S6.7 WS gateway.health" "got: $GWHEALTH"
fi

# S6.8 WS gateway.status
GWSTATUS=$(ws_rpc rpc gateway.status)
if [ $? -eq 0 ] && echo "$GWSTATUS" | jq -e '.running == true' >/dev/null 2>&1; then
    pass "S6.8 WS gateway.status"
else
    fail "S6.8 WS gateway.status" "got: $GWSTATUS"
fi

echo ""

# ==========================================================
# Phase 7 — Stability (concurrent connections)
# ==========================================================

echo "--- Phase 7: Stability ---"

# S7.1 10 concurrent WS connections
CONC=$(node "$WS_CONCURRENT" "ws://127.0.0.1:${WS_PORT}" "$TOKEN" 10 2>/dev/null)
if [ $? -eq 0 ] && echo "$CONC" | jq -e '.fail == 0' >/dev/null 2>&1; then
    pass "S7.1 10 concurrent WS connections"
else
    fail "S7.1 10 concurrent WS connections" "got: $CONC"
fi

# S7.2 Gateway still healthy after concurrent test
FINAL_HEALTH=$(http_get "http://127.0.0.1:${HTTP_PORT}/api/health")
if echo "$FINAL_HEALTH" | jq -e '.status == "ok"' >/dev/null 2>&1; then
    pass "S7.2 Gateway healthy after stress"
else
    fail "S7.2 Gateway healthy after stress" "got: $FINAL_HEALTH"
fi

echo ""

# ==========================================================
# Teardown
# ==========================================================

echo "--- Teardown ---"

kill -TERM "$GATEWAY_PID" 2>/dev/null || true
set +e
wait "$GATEWAY_PID" 2>/dev/null
EXIT_CODE=$?
set -e
GATEWAY_PID=""

if [ "$EXIT_CODE" -eq 0 ] || [ "$EXIT_CODE" -eq 143 ]; then
    pass "Graceful shutdown (exit=$EXIT_CODE)"
else
    fail "Graceful shutdown" "unexpected exit code $EXIT_CODE"
fi

echo ""

# ==========================================================
# Summary
# ==========================================================

echo "=== Results: $PASS passed, $FAIL failed, $SKIP skipped (of $TOTAL tests) ==="
echo "  Logs: ${LOG_DIR}/gateway.log"

if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo "--- Gateway log (last 30 lines) ---"
    tail -30 "${LOG_DIR}/gateway.log" 2>/dev/null || true
    exit 1
fi
exit 0
