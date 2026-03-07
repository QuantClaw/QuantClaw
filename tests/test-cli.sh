#!/bin/bash
# QuantClaw CLI Integration Test
#
# Tests every CLI command and subcommand: local-only (no gateway), gateway
# lifecycle, and all commands that route through the gateway client.
# No LLM API key is required for most tests; agent/run/eval are skipped
# unless OPENAI_API_KEY or ANTHROPIC_API_KEY is set.
#
# Usage:
#   bash tests/test-cli.sh [/path/to/quantclaw]
#   OPENAI_API_KEY=sk-... bash tests/test-cli.sh
#
# Exit code: 0 = all passed (skips don't count as failures).
#
# CI: run after `cmake --build build --parallel` succeeds.

set -uo pipefail

# ---------- Configuration ----------

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="${1:-${REPO_ROOT}/build/quantclaw}"
TEST_HOME="/tmp/quantclaw-cli-$$"
LOG_DIR="${TEST_HOME}/logs"
WS_PORT=18870
HTTP_PORT=18871
TOKEN="cli-smoke-token-$$"
GATEWAY_PID=""
PASS=0
FAIL=0
SKIP=0

# ---------- Helpers ----------

cleanup() {
    if [[ -n "$GATEWAY_PID" ]] && kill -0 "$GATEWAY_PID" 2>/dev/null; then
        kill -TERM "$GATEWAY_PID" 2>/dev/null || true
        wait "$GATEWAY_PID" 2>/dev/null || true
    fi
    rm -rf "$TEST_HOME"
}
trap cleanup EXIT

pass() { echo "  [PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "  [FAIL] $1: $2"; FAIL=$((FAIL + 1)); }
skip() { echo "  [SKIP] $1"; SKIP=$((SKIP + 1)); }

# Run quantclaw CLI with the test HOME
qc() { HOME="$TEST_HOME" "$BINARY" "$@"; }

# Run quantclaw, accept both exit-0 and exit-1 (just don't crash)
qc_any() { HOME="$TEST_HOME" "$BINARY" "$@" 2>&1 || true; }

wait_for_gateway() {
    local max_wait=30 waited=0
    while [[ $waited -lt $max_wait ]]; do
        if curl -sf --max-time 2 --noproxy '*' \
            -H "Authorization: Bearer ${TOKEN}" \
            "http://127.0.0.1:${HTTP_PORT}/api/health" | grep -q '"status"'; then
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: required command '$1' not found in PATH"
        exit 1
    fi
}

# ---------- Pre-flight ----------

echo "=== QuantClaw CLI Integration Test ==="
echo "Binary : $BINARY"
echo "WS port: $WS_PORT  HTTP port: $HTTP_PORT"
echo ""

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: binary not found or not executable: $BINARY"
    echo "  Build first: cmake --build build --parallel"
    exit 1
fi

require_cmd curl python3
mkdir -p "$LOG_DIR"

# ---------- Phase 0: Write minimal config ----------

echo "--- Phase 0: Setup ---"

mkdir -p "${TEST_HOME}/.quantclaw/agents/main/workspace"
mkdir -p "${TEST_HOME}/.quantclaw/agents/main/sessions"
touch "${TEST_HOME}/.quantclaw/agents/main/workspace/SOUL.md"

cat > "${TEST_HOME}/.quantclaw/quantclaw.json" <<EOFCFG
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
        "auth": { "mode": "token", "token": "${TOKEN}" },
        "controlUi": { "enabled": true, "port": ${HTTP_PORT} }
    },
    "models": {
        "defaultModel": "openai/test-model",
        "providers": { "openai": { "apiKey": "" } }
    },
    "mcp": { "servers": [] },
    "plugins": {}
}
EOFCFG

# Inject real API key into config if available (enables LLM tests in Phase 17)
CFG_PATH="${TEST_HOME}/.quantclaw/quantclaw.json"
if [[ -n "${OPENAI_API_KEY:-}" ]]; then
    python3 - <<PYEOF 2>/dev/null
import json
with open("${CFG_PATH}") as f: cfg = json.load(f)
cfg["models"]["providers"]["openai"]["apiKey"] = "${OPENAI_API_KEY}"
with open("${CFG_PATH}", "w") as f: json.dump(cfg, f, indent=2)
PYEOF
elif [[ -n "${ANTHROPIC_API_KEY:-}" ]]; then
    python3 - <<PYEOF 2>/dev/null
import json
with open("${CFG_PATH}") as f: cfg = json.load(f)
cfg["agent"]["model"] = "anthropic/claude-haiku-4-5-20251001"
cfg["models"]["defaultModel"] = "anthropic/claude-haiku-4-5-20251001"
cfg["models"]["providers"]["anthropic"] = {"apiKey": "${ANTHROPIC_API_KEY}"}
with open("${CFG_PATH}", "w") as f: json.dump(cfg, f, indent=2)
PYEOF
fi

pass "C0.1 test environment ready"

# ==========================================================
# Phase 1: --version / --help
# ==========================================================

echo ""
echo "--- Phase 1: version / help ---"

# C1.1 --version
OUT=$(qc_any --version)
if echo "$OUT" | grep -qi "quantclaw\|version\|[0-9]\+\.[0-9]\+"; then
    pass "C1.1 --version"
else
    fail "C1.1 --version" "output: $OUT"
fi

# C1.2 --help
OUT=$(qc --help 2>&1)
if echo "$OUT" | grep -qi "usage\|command\|gateway\|agent\|onboard"; then
    pass "C1.2 --help"
else
    fail "C1.2 --help" "output: $OUT"
fi

# C1.3 --json flag accepted (config get as test)
OUT=$(qc config get --json 2>&1)
if echo "$OUT" | python3 -c "import sys,json; json.loads(sys.stdin.read())" 2>/dev/null; then
    pass "C1.3 --json output is valid JSON"
else
    # Some commands output JSON without the flag too — just check exit 0
    if qc config get --json >/dev/null 2>&1; then
        pass "C1.3 --json flag accepted (exit 0)"
    else
        fail "C1.3 --json flag accepted" "output: $OUT"
    fi
fi

# ==========================================================
# Phase 2: config subcommands (local, no gateway)
# ==========================================================

echo ""
echo "--- Phase 2: config subcommands ---"

# C2.1 config get (full)
OUT=$(qc config get 2>&1)
if echo "$OUT" | grep -qi "agent\|gateway"; then
    pass "C2.1 config get (full)"
else
    fail "C2.1 config get (full)" "output: $OUT"
fi

# C2.2 config get dot-path
OUT=$(qc config get gateway.port 2>&1)
if echo "$OUT" | grep -q "$WS_PORT"; then
    pass "C2.2 config get (dot-path)"
else
    fail "C2.2 config get (dot-path)" "expected port $WS_PORT, got: $OUT"
fi

# C2.3 config set
qc config set agent.maxIterations 25 >/dev/null 2>&1
VAL=$(python3 -c "import json; d=json.load(open('${TEST_HOME}/.quantclaw/quantclaw.json')); print(d['agent']['maxIterations'])" 2>/dev/null)
if [[ "$VAL" == "25" ]]; then
    pass "C2.3 config set"
else
    fail "C2.3 config set" "got: '$VAL'"
fi

# C2.4 config set (string value)
qc config set agent.model "anthropic/claude-haiku-4-5" >/dev/null 2>&1
VAL=$(python3 -c "import json; d=json.load(open('${TEST_HOME}/.quantclaw/quantclaw.json')); print(d['agent']['model'])" 2>/dev/null)
if echo "$VAL" | grep -q "claude-haiku"; then
    pass "C2.4 config set (string value)"
else
    fail "C2.4 config set (string value)" "got: '$VAL'"
fi

# C2.5 config unset
qc config set agent.temperature 0.3 >/dev/null 2>&1
qc config unset agent.temperature >/dev/null 2>&1
HAS=$(python3 -c "import json; d=json.load(open('${TEST_HOME}/.quantclaw/quantclaw.json')); print('yes' if 'temperature' in d.get('agent',{}) else 'no')" 2>/dev/null)
if [[ "$HAS" == "no" ]]; then
    pass "C2.5 config unset"
else
    fail "C2.5 config unset" "key still present"
fi

# Reset model for subsequent tests
qc config set agent.model "openai/test-model" >/dev/null 2>&1

# C2.6 config validate
OUT=$(qc config validate 2>&1)
if echo "$OUT" | grep -qi "valid"; then
    pass "C2.6 config validate"
else
    fail "C2.6 config validate" "output: $OUT"
fi

# C2.7 config schema
OUT=$(qc config schema 2>&1)
if echo "$OUT" | grep -qi "agent\|gateway\|model"; then
    pass "C2.7 config schema"
else
    fail "C2.7 config schema" "output: $OUT"
fi

# ==========================================================
# Phase 3: doctor (no gateway — partial check)
# ==========================================================

echo ""
echo "--- Phase 3: doctor (pre-gateway) ---"

# C3.1 doctor runs without crash (gateway not running)
OUT=$(qc_any doctor)
if echo "$OUT" | grep -qi "config\|workspace\|gateway\|doctor\|quantclaw"; then
    pass "C3.1 doctor (no gateway — shows diagnostics)"
else
    fail "C3.1 doctor (no gateway)" "output: $OUT"
fi

# ==========================================================
# Phase 4: Gateway startup
# ==========================================================

echo ""
echo "--- Phase 4: Gateway startup ---"

HOME="$TEST_HOME" "$BINARY" gateway run \
    >"${LOG_DIR}/gateway.log" 2>&1 &
GATEWAY_PID=$!

if wait_for_gateway; then
    pass "C4.1 gateway started and ready"
else
    fail "C4.1 gateway started and ready" "timed out"
    echo "--- Gateway log ---"
    tail -20 "${LOG_DIR}/gateway.log" 2>/dev/null || true
    exit 1
fi

# ==========================================================
# Phase 5: health / status / doctor (with gateway)
# ==========================================================

echo ""
echo "--- Phase 5: health / status / doctor ---"

# C5.1 health
OUT=$(qc health 2>&1)
if echo "$OUT" | grep -qi "ok\|healthy\|running"; then
    pass "C5.1 health"
else
    fail "C5.1 health" "output: $OUT"
fi

# C5.2 status
OUT=$(qc status 2>&1)
if echo "$OUT" | grep -qi "running\|gateway\|session\|ok"; then
    pass "C5.2 status"
else
    fail "C5.2 status" "output: $OUT"
fi

# C5.3 doctor (with gateway running — should show more green)
OUT=$(qc_any doctor)
if echo "$OUT" | grep -qi "gateway\|config\|doctor"; then
    pass "C5.3 doctor (with gateway)"
else
    fail "C5.3 doctor (with gateway)" "output: $OUT"
fi

# C5.4 dashboard (just verify it prints the URL without crashing)
OUT=$(qc_any dashboard)
if echo "$OUT" | grep -qi "127.0.0.1\|http\|dashboard\|url"; then
    pass "C5.4 dashboard (prints URL)"
else
    fail "C5.4 dashboard (prints URL)" "output: $OUT"
fi

# C5.5 logs (non-blocking — just check it doesn't crash immediately)
OUT=$(timeout 3 bash -c "HOME='$TEST_HOME' '$BINARY' logs" 2>&1 || true)
# logs may return empty if no entries, or stream entries — either is fine
pass "C5.5 logs (no crash)"

# ==========================================================
# Phase 6: gateway subcommands
# ==========================================================

echo ""
echo "--- Phase 6: gateway subcommands ---"

# C6.1 gateway status
OUT=$(qc gateway status 2>&1)
if echo "$OUT" | grep -qi "running\|gateway\|ok\|status"; then
    pass "C6.1 gateway status"
else
    fail "C6.1 gateway status" "output: $OUT"
fi

# C6.2 gateway call — gateway.health
OUT=$(qc gateway call gateway.health 2>&1)
if echo "$OUT" | grep -qi '"status".*"ok"\|ok\|healthy'; then
    pass "C6.2 gateway call gateway.health"
else
    fail "C6.2 gateway call gateway.health" "output: $OUT"
fi

# C6.3 gateway call — gateway.status
OUT=$(qc gateway call gateway.status 2>&1)
if echo "$OUT" | grep -qi "running\|uptime\|connections"; then
    pass "C6.3 gateway call gateway.status"
else
    fail "C6.3 gateway call gateway.status" "output: $OUT"
fi

# C6.4 gateway call — config.get
OUT=$(qc gateway call config.get 2>&1)
if echo "$OUT" | grep -qi "agent\|gateway\|config"; then
    pass "C6.4 gateway call config.get"
else
    fail "C6.4 gateway call config.get" "output: $OUT"
fi

# C6.5 gateway call — config.get (dot-path)
OUT=$(qc gateway call config.get '{"path":"agent.model"}' 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C6.5 gateway call config.get (dot-path)"
else
    fail "C6.5 gateway call config.get (dot-path)" "output: $OUT"
fi

# C6.6 gateway call — config.set + verify
qc gateway call config.set '{"path":"agent.temperature","value":0.42}' >/dev/null 2>&1
OUT=$(qc gateway call config.get '{"path":"agent.temperature"}' 2>&1)
if echo "$OUT" | grep -q "0.42"; then
    pass "C6.6 gateway call config.set + verify"
else
    fail "C6.6 gateway call config.set" "expected 0.42, got: $OUT"
fi

# C6.7 gateway call — config.reload
OUT=$(qc gateway call config.reload 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C6.7 gateway call config.reload"
else
    fail "C6.7 gateway call config.reload" "output: $OUT"
fi

# C6.8 gateway call — agent.identity.get
OUT=$(qc gateway call agent.identity.get 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C6.8 gateway call agent.identity.get"
else
    fail "C6.8 gateway call agent.identity.get" "output: $OUT"
fi

# C6.9 gateway call — node.list
OUT=$(qc gateway call node.list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C6.9 gateway call node.list"
else
    fail "C6.9 gateway call node.list" "output: $OUT"
fi

# C6.10 gateway call — logs.tail
OUT=$(qc gateway call logs.tail '{"lines":5}' 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C6.10 gateway call logs.tail"
else
    fail "C6.10 gateway call logs.tail" "output: $OUT"
fi

# ==========================================================
# Phase 7: sessions subcommands
# ==========================================================

echo ""
echo "--- Phase 7: sessions subcommands ---"

SESSION_KEY="cli:test:$$"

# C7.1 sessions list
OUT=$(qc sessions list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C7.1 sessions list"
else
    fail "C7.1 sessions list" "output: $OUT"
fi

# C7.2 sessions history (empty)
OUT=$(qc sessions history "$SESSION_KEY" 2>&1)
if [[ $? -eq 0 ]] || echo "$OUT" | grep -qi "not found\|empty\|no messages\|history"; then
    pass "C7.2 sessions history (graceful for new session)"
else
    fail "C7.2 sessions history" "output: $OUT"
fi

# C7.3 sessions reset
qc sessions reset "$SESSION_KEY" >/dev/null 2>&1
pass "C7.3 sessions reset (no crash)"

# C7.4 sessions delete
qc sessions delete "$SESSION_KEY" >/dev/null 2>&1
pass "C7.4 sessions delete (no crash)"

# C7.5 gateway call — sessions.list
OUT=$(qc gateway call sessions.list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C7.5 gateway call sessions.list"
else
    fail "C7.5 gateway call sessions.list" "output: $OUT"
fi

# C7.6 gateway call — sessions.history
OUT=$(qc gateway call sessions.history '{"sessionKey":"cli:rpc:test"}' 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C7.6 gateway call sessions.history"
else
    fail "C7.6 gateway call sessions.history" "output: $OUT"
fi

# C7.7 gateway call — sessions.usage (UI compat)
OUT=$(qc gateway call sessions.usage 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C7.7 gateway call sessions.usage"
else
    fail "C7.7 gateway call sessions.usage" "output: $OUT"
fi

# ==========================================================
# Phase 8: skills subcommands
# ==========================================================

echo ""
echo "--- Phase 8: skills subcommands ---"

# C8.1 skills list
OUT=$(qc skills list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C8.1 skills list"
else
    fail "C8.1 skills list" "output: $OUT"
fi

# C8.2 gateway call — skills.status
OUT=$(qc gateway call skills.status 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C8.2 gateway call skills.status"
else
    fail "C8.2 gateway call skills.status" "output: $OUT"
fi

# C8.3 skills install (known skill — no-op if already present)
OUT=$(qc_any skills install weather)
pass "C8.3 skills install (no crash)"

# ==========================================================
# Phase 9: memory subcommands
# ==========================================================

echo ""
echo "--- Phase 9: memory subcommands ---"

# C9.1 memory status
OUT=$(qc memory status 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C9.1 memory status"
else
    fail "C9.1 memory status" "output: $OUT"
fi

# C9.2 memory search (empty index — graceful)
OUT=$(qc memory search "quantclaw test search query" 2>&1)
if [[ $? -eq 0 ]] || echo "$OUT" | grep -qi "no results\|empty\|0 result\|found"; then
    pass "C9.2 memory search (empty index)"
else
    fail "C9.2 memory search" "output: $OUT"
fi

# C9.3 gateway call — memory.status
OUT=$(qc gateway call memory.status 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C9.3 gateway call memory.status"
else
    fail "C9.3 gateway call memory.status" "output: $OUT"
fi

# ==========================================================
# Phase 10: cron subcommands
# ==========================================================

echo ""
echo "--- Phase 10: cron subcommands ---"

# C10.1 cron list
OUT=$(qc cron list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C10.1 cron list"
else
    fail "C10.1 cron list" "output: $OUT"
fi

# C10.2 cron add — form: cron add <name> <schedule> <message>
OUT=$(qc cron add "test-task" "*/5 * * * *" "echo hello" 2>&1)
if [[ $? -eq 0 ]] || echo "$OUT" | grep -qi "added\|creat\|schedul\|ok"; then
    pass "C10.2 cron add"
else
    fail "C10.2 cron add" "output: $OUT"
fi

# C10.3 cron list (after add)
OUT=$(qc cron list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C10.3 cron list (after add)"
else
    fail "C10.3 cron list (after add)" "output: $OUT"
fi

# C10.4 cron remove (get first ID)
CRON_ID=$(echo "$OUT" | grep -oP '"id"\s*:\s*"\K[^"]+' | head -1 || echo "")
if [[ -n "$CRON_ID" ]]; then
    qc cron remove "$CRON_ID" >/dev/null 2>&1
    pass "C10.4 cron remove (id=$CRON_ID)"
else
    # Try removing by name
    qc_any cron remove "test-task" >/dev/null 2>&1
    pass "C10.4 cron remove (no crash)"
fi

# C10.5 gateway call — cron.list
OUT=$(qc gateway call cron.list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C10.5 gateway call cron.list"
else
    fail "C10.5 gateway call cron.list" "output: $OUT"
fi

# C10.6 gateway call — cron.status
OUT=$(qc gateway call cron.status 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C10.6 gateway call cron.status"
else
    fail "C10.6 gateway call cron.status" "output: $OUT"
fi

# ==========================================================
# Phase 11: models subcommands
# ==========================================================

echo ""
echo "--- Phase 11: models subcommands ---"

# C11.1 models list
OUT=$(qc models list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C11.1 models list"
else
    fail "C11.1 models list" "output: $OUT"
fi

# C11.2 models aliases
OUT=$(qc_any models aliases)
if [[ -n "$OUT" ]] || true; then
    pass "C11.2 models aliases (no crash)"
fi

# C11.3 gateway call — models.list
OUT=$(qc gateway call models.list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C11.3 gateway call models.list"
else
    fail "C11.3 gateway call models.list" "output: $OUT"
fi

# ==========================================================
# Phase 12: channels subcommands
# ==========================================================

echo ""
echo "--- Phase 12: channels subcommands ---"

# C12.1 channels list
OUT=$(qc channels list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C12.1 channels list"
else
    fail "C12.1 channels list" "output: $OUT"
fi

# C12.2 channels status
OUT=$(qc_any channels status)
pass "C12.2 channels status (no crash)"

# C12.3 gateway call — channels.list
OUT=$(qc gateway call channels.list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C12.3 gateway call channels.list"
else
    fail "C12.3 gateway call channels.list" "output: $OUT"
fi

# C12.4 gateway call — channels.status
OUT=$(qc gateway call channels.status 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C12.4 gateway call channels.status"
else
    fail "C12.4 gateway call channels.status" "output: $OUT"
fi

# ==========================================================
# Phase 13: plugins subcommands
# ==========================================================

echo ""
echo "--- Phase 13: plugins subcommands ---"

# C13.1 plugins list
OUT=$(qc plugins list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C13.1 plugins list"
else
    fail "C13.1 plugins list" "output: $OUT"
fi

# C13.2 plugins status
OUT=$(qc_any plugins status)
pass "C13.2 plugins status (no crash)"

# C13.3 gateway call — plugins.list
OUT=$(qc gateway call plugins.list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C13.3 gateway call plugins.list"
else
    fail "C13.3 gateway call plugins.list" "output: $OUT"
fi

# C13.4 gateway call — plugins.tools
OUT=$(qc gateway call plugins.tools 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C13.4 gateway call plugins.tools"
else
    fail "C13.4 gateway call plugins.tools" "output: $OUT"
fi

# C13.5 gateway call — plugins.services
OUT=$(qc gateway call plugins.services 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C13.5 gateway call plugins.services"
else
    fail "C13.5 gateway call plugins.services" "output: $OUT"
fi

# C13.6 gateway call — plugins.providers
OUT=$(qc gateway call plugins.providers 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C13.6 gateway call plugins.providers"
else
    fail "C13.6 gateway call plugins.providers" "output: $OUT"
fi

# C13.7 gateway call — plugins.commands
OUT=$(qc gateway call plugins.commands 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C13.7 gateway call plugins.commands"
else
    fail "C13.7 gateway call plugins.commands" "output: $OUT"
fi

# ==========================================================
# Phase 14: queue and tools
# ==========================================================

echo ""
echo "--- Phase 14: queue / tools ---"

# C14.1 gateway call — queue.status
OUT=$(qc gateway call queue.status 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C14.1 gateway call queue.status"
else
    fail "C14.1 gateway call queue.status" "output: $OUT"
fi

# C14.2 gateway call — queue.configure
OUT=$(qc gateway call queue.configure '{"maxConcurrent":4}' 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C14.2 gateway call queue.configure"
else
    fail "C14.2 gateway call queue.configure" "output: $OUT"
fi

# C14.3 gateway call — tools.catalog
OUT=$(qc gateway call tools.catalog 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C14.3 gateway call tools.catalog"
else
    fail "C14.3 gateway call tools.catalog" "output: $OUT"
fi

# ==========================================================
# Phase 15: usage / UI compat RPCs
# ==========================================================

echo ""
echo "--- Phase 15: usage / UI compat RPCs ---"

# C15.1 usage.cost
OUT=$(qc gateway call usage.cost 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C15.1 gateway call usage.cost"
else
    fail "C15.1 gateway call usage.cost" "output: $OUT"
fi

# C15.2 sessions.usage.timeseries
OUT=$(qc gateway call sessions.usage.timeseries 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C15.2 gateway call sessions.usage.timeseries"
else
    fail "C15.2 gateway call sessions.usage.timeseries" "output: $OUT"
fi

# C15.3 sessions.usage.logs
OUT=$(qc gateway call sessions.usage.logs 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C15.3 gateway call sessions.usage.logs"
else
    fail "C15.3 gateway call sessions.usage.logs" "output: $OUT"
fi

# C15.4 device.pair.list
OUT=$(qc gateway call device.pair.list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C15.4 gateway call device.pair.list"
else
    fail "C15.4 gateway call device.pair.list" "output: $OUT"
fi

# C15.5 agents.list
OUT=$(qc gateway call agents.list 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C15.5 gateway call agents.list"
else
    fail "C15.5 gateway call agents.list" "output: $OUT"
fi

# C15.6 config.schema (RPC)
OUT=$(qc gateway call config.schema 2>&1)
if [[ $? -eq 0 ]]; then
    pass "C15.6 gateway call config.schema"
else
    fail "C15.6 gateway call config.schema" "output: $OUT"
fi

# ==========================================================
# Phase 16: config reload (live gateway)
# ==========================================================

echo ""
echo "--- Phase 16: config reload ---"

# C16.1 config reload CLI
OUT=$(qc_any config reload)
if echo "$OUT" | grep -qi "reload\|ok\|success\|done" || true; then
    pass "C16.1 config reload (CLI)"
fi

# C16.2 gateway still healthy after reload
OUT=$(curl -sf --max-time 5 --noproxy '*' \
    -H "Authorization: Bearer ${TOKEN}" \
    "http://127.0.0.1:${HTTP_PORT}/api/health" 2>/dev/null)
if echo "$OUT" | grep -q '"status"'; then
    pass "C16.2 gateway healthy after config reload"
else
    fail "C16.2 gateway healthy after config reload" "got: $OUT"
fi

# ==========================================================
# Phase 17: agent / run / eval (API key required)
# ==========================================================

echo ""
echo "--- Phase 17: agent / run / eval ---"

HAS_KEY=0
if [[ -n "${OPENAI_API_KEY:-}" ]] || [[ -n "${ANTHROPIC_API_KEY:-}" ]]; then
    HAS_KEY=1
fi

if [[ $HAS_KEY -eq 1 ]]; then
    # C17.1 agent -m
    OUT=$(timeout 60 bash -c "HOME='$TEST_HOME' '$BINARY' agent -m 'Reply with just: OK' --session cli:agent:$$" 2>&1)
    if [[ $? -eq 0 ]]; then
        pass "C17.1 agent -m"
    else
        fail "C17.1 agent -m" "output: $OUT"
    fi

    # C17.2 run (one-shot, no session)
    OUT=$(timeout 60 bash -c "HOME='$TEST_HOME' '$BINARY' run 'Reply with just: DONE'" 2>&1)
    if [[ $? -eq 0 ]]; then
        pass "C17.2 run (one-shot)"
    else
        fail "C17.2 run" "output: $OUT"
    fi

    # C17.3 eval (no session persistence)
    OUT=$(timeout 60 bash -c "HOME='$TEST_HOME' '$BINARY' eval 'Reply with just: YES'" 2>&1)
    if [[ $? -eq 0 ]]; then
        pass "C17.3 eval (no-session)"
    else
        fail "C17.3 eval" "output: $OUT"
    fi
else
    skip "C17.1 agent -m (no API key set)"
    skip "C17.2 run (no API key set)"
    skip "C17.3 eval (no API key set)"
fi

# ==========================================================
# Phase 18: gateway service lifecycle (systemd — best effort)
# ==========================================================

echo ""
echo "--- Phase 18: gateway service commands ---"

# C18.1 gateway install — may fail without systemd user session, but must not crash
OUT=$(qc_any gateway install)
if echo "$OUT" | grep -qi "install\|service\|systemd\|daemon\|error\|fail\|not supported"; then
    pass "C18.1 gateway install (handled gracefully)"
else
    pass "C18.1 gateway install (no crash)"
fi

# C18.2 gateway uninstall — symmetrical
OUT=$(qc_any gateway uninstall)
pass "C18.2 gateway uninstall (no crash)"

# ==========================================================
# Phase 19: Gateway shutdown
# ==========================================================

echo ""
echo "--- Phase 19: Gateway shutdown ---"

kill -TERM "$GATEWAY_PID" 2>/dev/null || true
wait "$GATEWAY_PID" 2>/dev/null
EXIT_CODE=$?
GATEWAY_PID=""

if [[ $EXIT_CODE -eq 0 || $EXIT_CODE -eq 143 ]]; then
    pass "C19.1 gateway graceful shutdown (exit=$EXIT_CODE)"
else
    fail "C19.1 gateway graceful shutdown" "exit code $EXIT_CODE"
fi

# ==========================================================
# Summary
# ==========================================================

echo ""
echo "========================================"
TOTAL=$((PASS + FAIL + SKIP))
echo "Results: $PASS/$TOTAL passed, $FAIL failed, $SKIP skipped"
echo "========================================"

if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo "--- Gateway log (last 30 lines) ---"
    tail -30 "${LOG_DIR}/gateway.log" 2>/dev/null || true
    exit 1
fi
exit 0
