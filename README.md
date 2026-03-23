<p align="center">
  <img src="assets/quantclaw-logo-white-0307.png" alt="QuantClaw" width="180" />
</p>

<h1 align="center">QuantClaw</h1>

<p align="center">
  <strong>High-performance personal AI assistant in C++17</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache%202.0-blue.svg" alt="License"></a>
  <a href="https://github.com/QuantClaw/QuantClaw/actions/workflows/github-actions.yml"><img src="https://github.com/QuantClaw/QuantClaw/actions/workflows/github-actions.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C.svg?logo=cplusplus&logoColor=white" alt="C++17">
  <img src="https://img.shields.io/badge/tests-996%20passing-brightgreen.svg" alt="996 tests passing">
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg" alt="Linux | Windows">
  <a href="https://github.com/openclaw/openclaw"><img src="https://img.shields.io/badge/OpenClaw-compatible-orange.svg" alt="OpenClaw Compatible"></a>
</p>

<p align="center">
  <a href="README_CN.md">中文文档</a>
</p>

---

QuantClaw is a native C++ implementation of the [OpenClaw](https://github.com/openclaw/openclaw) ecosystem — built for performance and low memory footprint while staying fully compatible with OpenClaw workspace files, skills, and the WebSocket RPC protocol.

## Features

- **Blazing Fast**: C++17 native performance with minimal overhead
- **Memory Efficient**: Small memory footprint, suitable for resource-constrained environments
- **OpenClaw Compatible**: Works with OpenClaw workspace files, skills, and configuration
- **Dual Protocol**: WebSocket RPC gateway + HTTP REST API
- **Multi-Provider LLM**: OpenAI-compatible and Anthropic APIs with `provider/model` prefix routing
- **Model Failover**: Multi-key rotation with exponential backoff cooldowns and automatic model fallback chains
- **Command Queue**: Per-session serialization with collect/followup/steer/interrupt modes and global concurrency control
- **Context Management**: Auto-compaction, tool result pruning, and BM25 memory search
- **Channel Adapters**: Connect Discord, Telegram, or custom bots to the gateway
- **Session Persistence**: Full conversation history with tool call context preserved in JSONL
- **Skill System**: Compatible with OpenClaw SKILL.md format (both OpenClaw and QuantClaw manifest formats)
- **Plugin Ecosystem**: Full OpenClaw plugin compatibility via Node.js sidecar — tools, hooks, services, providers, commands, HTTP routes, and gateway methods
- **MCP Support**: Model Context Protocol for external tool integration
- **File System First**: No database dependencies — everything stored in your workspace

## 📖 Documentation

Full documentation available at: **[https://quantclaw.github.io/](https://quantclaw.github.io/)**

Includes:
- [Getting Started Guide](https://quantclaw.github.io/guide/getting-started)
- [Installation Instructions](https://quantclaw.github.io/guide/installation)
- [Architecture Overview](https://quantclaw.github.io/guide/architecture)
- [Plugin Development Guide](https://quantclaw.github.io/guide/plugins)
- [CLI Reference](https://quantclaw.github.io/guide/cli-reference)

## Quick Start

### 1. Build QuantClaw

```bash
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run tests
./quantclaw_tests

# Install (optional)
sudo make install
```

### 2. Run Onboarding Wizard

```bash
# Interactive setup wizard (recommended)
quantclaw onboard

# Or with automatic daemon installation
quantclaw onboard --install-daemon

# Or quick setup without prompts
quantclaw onboard --quick
```

The onboarding wizard guides you through:
- Configuration setup (gateway port, AI model, etc.)
- Workspace creation (SOUL.md, skills directory, etc.)
- Optional daemon installation as system service
- Skills initialization
- Setup verification

### 3. Start the Gateway

```bash
# If installed as service
quantclaw gateway start

# Or run in foreground
quantclaw gateway
```

### 4. Open Dashboard

```bash
# Build UI
./scripts/build_ui.sh

# Open Dashboard
quantclaw dashboard
```

This opens the web UI at `http://127.0.0.1:18801`

## Dashboard Authentication

The dashboard requires a token to access. The token is defined in your configuration file:

```json
{
  "gateway": {
    "auth": {
      "mode": "token",
      "token": "YOUR_SECRET_TOKEN"
    }
  }
}
```

**First-time access:**
1. Open `http://127.0.0.1:18801` in your browser
2. Enter the token you configured in `~/.quantclaw/quantclaw.json`
3. The token is stored in your browser's localStorage for future visits

**To disable authentication** (not recommended for production):
```json
{
  "gateway": {
    "auth": {
      "mode": "none"
    }
  }
}
```

**To change your token:**
1. Edit `~/.quantclaw/quantclaw.json` and update `gateway.auth.token`
2. Run `quantclaw config reload` (or restart the gateway)
3. Clear your browser's localStorage for `127.0.0.1:18801` and enter the new token

## Port Configuration

QuantClaw uses dedicated ports to avoid conflicts with OpenClaw and other services:

| Service | Port | Purpose |
|---------|------|---------|
| WebSocket RPC Gateway | `18800` | Main gateway for client connections |
| HTTP REST API / Dashboard | `18801` | Control UI and REST API endpoints |
| Sidecar IPC (TCP loopback) | `18802-18899` | Node.js Sidecar process communication |

**Note**: QuantClaw uses ports `18800-18801` (different from OpenClaw's `18789-18790`), allowing both to run simultaneously.

To use custom ports, edit `~/.quantclaw/quantclaw.json`:

```json
{
  "gateway": {
    "port": 18800,
    "controlUi": {
      "port": 18801
    }
  }
}
```

## Architecture

```
~/.quantclaw/
├── quantclaw.json              # Configuration (OpenClaw format)
├── skills/                     # Installed skills (OpenClaw compatible)
│   └── weather/
│       └── SKILL.md
└── agents/main/
    ├── workspace/
    │   ├── SOUL.md             # Assistant identity and values
    │   ├── IDENTITY.md         # Self-description and capabilities
    │   ├── MEMORY.md           # Long-term memory
    │   ├── SKILL.md            # Available skills declaration
    │   ├── HEARTBEAT.md        # Periodic status / cron log
    │   ├── USER.md             # User profile and preferences
    │   ├── AGENTS.md           # Known agent roster
    │   └── TOOLS.md            # Tool usage guidelines
    └── sessions/
        ├── sessions.json       # Session index
        └── <session-id>.jsonl  # Per-session transcript
```

## Configuration

QuantClaw uses JSON configuration (`~/.quantclaw/quantclaw.json`):

```json
{
  "system": {
    "logLevel": "info"
  },
  "llm": {
    "model": "openai/qwen-max",
    "maxIterations": 15,
    "temperature": 0.7,
    "maxTokens": 4096
  },
  "providers": {
    "openai": {
      "apiKey": "YOUR_OPENAI_API_KEY",
      "baseUrl": "https://api.openai.com/v1",
      "timeout": 30
    },
    "anthropic": {
      "apiKey": "YOUR_ANTHROPIC_API_KEY",
      "baseUrl": "https://api.anthropic.com",
      "timeout": 30
    }
  },
  "gateway": {
    "port": 18800,
    "bind": "loopback",
    "auth": { "mode": "token", "token": "YOUR_SECRET_TOKEN" },
    "controlUi": { "enabled": true, "port": 18801 }
  },
  "channels": {
    "discord": { "enabled": false, "token": "YOUR_DISCORD_BOT_TOKEN", "allowedIds": [] },
    "telegram": { "enabled": false, "token": "YOUR_TELEGRAM_BOT_TOKEN", "allowedIds": [] }
  },
  "tools": {
    "allow": ["group:fs", "group:runtime"],
    "deny": []
  },
  "security": {
    "sandbox": {
      "enabled": true,
      "allowedPaths": ["~/.quantclaw/agents/main/workspace"],
      "deniedPaths": ["/etc", "/sys", "/proc"]
    }
  },
  "mcp": {
    "servers": []
  }
}
```

The model field uses `provider/model-name` prefix routing. If no prefix is given, it defaults to `openai`. See `config.example.json` for a full example with all options.

### Log Retention

QuantClaw enforces automatic log cleanup on every gateway startup to prevent disk exhaustion.

| Option | Key | Default | Description |
|--------|-----|---------|-------------|
| Retention period | `system.logRetentionDays` | `7` | Delete `.log` files older than N days. Set to `0` to keep forever. |
| Total size cap | `system.logMaxSizeMb` | `50` | Maximum total log storage in MiB, split across 5 rotating files (~10 MiB each). |

Log files are stored at `~/.quantclaw/logs/`. The main application log (`quantclaw.log`) is size-rotated automatically by spdlog; the gateway service log (`gateway.log`, written by systemd) is time-pruned at every startup.

### Dependencies

**Required (system packages)**:
- C++17 compiler (GCC 7+ or Clang 5+)
- spdlog — logging
- nlohmann/json — JSON library
- libcurl — HTTP client
- OpenSSL — TLS/SSL

**Fetched automatically by CMake**:
- IXWebSocket 11.4.5 — WebSocket server/client
- cpp-httplib 0.18.3 — HTTP server
- Google Test 1.14.0 — testing framework

### Ubuntu / Debian one-liner

```bash
sudo apt install build-essential cmake libssl-dev \
  libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev zlib1g-dev
```

## Usage

### Onboarding Wizard

The easiest way to get started is the interactive onboarding wizard:

```bash
# Run the full wizard
quantclaw onboard

# Install daemon automatically
quantclaw onboard --install-daemon

# Quick setup (non-interactive)
quantclaw onboard --quick
```

The wizard creates:
- Configuration file (`~/.quantclaw/quantclaw.json`)
- Workspace directory (`~/.quantclaw/agents/main/workspace/`)
- SOUL.md (agent identity file)
- Optional systemd service for daemon mode

### Gateway (background service)

```bash
# Run gateway in foreground
quantclaw gateway

# Install as system service (systemd / launchd)
quantclaw gateway install

# Uninstall the service
quantclaw gateway uninstall

# Start / stop / restart daemon
quantclaw gateway start
quantclaw gateway stop
quantclaw gateway restart

# Check status
quantclaw gateway status

# Call any RPC method directly
quantclaw gateway call gateway.health
```

### Agent interaction

```bash
# Send a message
quantclaw agent "Hello, introduce yourself"

# With a custom session key
quantclaw agent --session my:session "What's the weather?"
```

### Session management

```bash
quantclaw sessions list
quantclaw sessions history <session-key>
quantclaw sessions delete <session-key>
quantclaw sessions reset <session-key>
```

### Config management

```bash
quantclaw config get                    # View full config
quantclaw config get llm.model          # View a specific value (dot-path)
quantclaw config set llm.model "anthropic/claude-sonnet-4-6"    # Change a value
quantclaw config unset llm.temperature                          # Remove a key
quantclaw config reload                 # Hot-reload config (no restart needed)
```

### Skills management

```bash
quantclaw skills list              # List loaded skills
quantclaw skills install <name>    # Install a skill's dependencies
```

### Memory search

```bash
quantclaw memory search "<query>"  # BM25 search across workspace memory files
quantclaw memory status            # Show memory index stats
```

### Cron scheduler

```bash
quantclaw cron list                            # List scheduled tasks
quantclaw cron add <name> <schedule> <task>    # Add a cron task (cron expression)
quantclaw cron remove <id>                     # Remove a task by ID
```

### Other commands

```bash
quantclaw health          # Quick health check
quantclaw logs            # Show last 50 lines of gateway logs
quantclaw logs -n 100     # Show last 100 lines
quantclaw logs -f         # Follow logs in real-time
quantclaw logs -n 20 -f   # Follow, starting from last 20 lines
quantclaw doctor          # Diagnostic check
quantclaw dashboard       # Open web UI in browser
```

> **Note for Windows users:** log following (`-f`) is not currently supported on Windows and will print an error and exit.
> As a workaround, use `quantclaw logs -n <count>` (for example, `quantclaw logs -n 100`) and re-run as needed.

### In-conversation message commands

While chatting, prefix a message with a slash command to control the session:

| Command | Effect |
|---------|--------|
| `/new` | Start a new session |
| `/reset` | Clear current session history |
| `/compact` | Manually trigger context compaction |
| `/status` | Show current session and queue status |
| `/commands` | List all available slash commands |
| `/help` | Show help text |

## Skill System

Skills extend the agent's capabilities by injecting contextual instructions and tools into the system prompt. Each skill is a directory containing a `SKILL.md` file.

### Built-in Skills

| Skill | Description | Always Active |
|-------|-------------|---------------|
| 🔍 `search` | Web search via `web_search` tool (Tavily → DuckDuckGo fallback) | Yes |
| 🌦️ `weather` | Check current weather via wttr.in | Yes |
| 🐙 `github` | Interact with GitHub using the `gh` CLI | No (requires `gh`) |
| 🏥 `healthcheck` | System health audit and diagnostics | Yes |
| 🎨 `skill-creator` | Guide for creating new skills | Yes |

### Creating Custom Skills

Place a skill directory in `~/.quantclaw/skills/` (global) or in the workspace:

```yaml
# skills/my-skill/SKILL.md
---
name: my-skill
emoji: "🔧"
description: What this skill does
requires:
  bins:
    - required-binary    # Must be installed for skill to activate
  env:
    - REQUIRED_ENV_VAR
always: false            # true = always injected; false = on-demand
metadata:
  openclaw:
    install:
      apt: package-name  # Auto-installed via `skills install`
      node: npm-package
---

Markdown instructions here — injected into the agent's system prompt when the skill is active.
```

Skills are compatible with the OpenClaw SKILL.md format.

## Channel Adapters

QuantClaw supports external channel adapters that connect to the gateway as standard WebSocket RPC clients. Adapters are Node.js processes managed by `ChannelAdapterManager`.

**Built-in adapters** (in `src/adapters/`):

| Adapter   | Library     | Status |
|-----------|-------------|--------|
| Discord   | discord.js  | Ready  |
| Telegram  | telegraf    | Ready  |

Enable a channel in your config:

```json
{
  "channels": {
    "discord": {
      "enabled": true,
      "token": "YOUR_DISCORD_BOT_TOKEN"
    }
  }
}
```

When the gateway starts, it launches enabled adapters automatically. Each adapter connects via `connect` + `chat.send` RPC calls — the same protocol any OpenClaw-compatible client uses.

## HTTP REST API

When the gateway is running, the HTTP API is available at `http://localhost:18801`:

```bash
# Health check
curl http://localhost:18801/api/health

# Gateway status
curl http://localhost:18801/api/status

# Send a message (non-streaming)
curl -X POST http://localhost:18801/api/agent/request \
  -H "Content-Type: application/json" \
  -d '{"message": "Hello!", "sessionKey": "my:session"}'

# List sessions
curl http://localhost:18801/api/sessions?limit=10

# Session history
curl "http://localhost:18801/api/sessions/history?sessionKey=my:session"
```

With authentication enabled, add the `Authorization` header:
```bash
curl -H "Authorization: Bearer YOUR_TOKEN" http://localhost:18801/api/status
```

### Plugin API endpoints

```bash
# List loaded plugins
curl http://localhost:18801/api/plugins

# Get tool schemas from plugins
curl http://localhost:18801/api/plugins/tools

# Call a plugin tool
curl -X POST http://localhost:18801/api/plugins/tools/my-tool \
  -H "Content-Type: application/json" \
  -d '{"arg1": "value"}'

# List plugin services / providers / commands
curl http://localhost:18801/api/plugins/services
curl http://localhost:18801/api/plugins/providers
curl http://localhost:18801/api/plugins/commands
```

## WebSocket RPC Protocol (OpenClaw Compatible)

The gateway exposes a WebSocket RPC interface on port 18800:

1. Client connects → server sends `connect.challenge` with nonce
2. Client responds with `connect.hello` containing auth token
3. Client sends JSON-RPC requests → server responds with results

**Available RPC methods**: `gateway.health`, `gateway.status`, `config.get`, `agent.request`, `agent.stop`, `sessions.list`, `sessions.history`, `sessions.delete`, `sessions.reset`, `channels.list`, `chain.execute`, `plugins.list`, `plugins.tools`, `plugins.call_tool`, `plugins.services`, `plugins.providers`, `plugins.commands`, `plugins.gateway`, `queue.status`, `queue.configure`, `queue.cancel`, `queue.abort`

Streaming responses emit real-time events: `text_delta`, `tool_use`, `tool_result`, `message_end`.

Any OpenClaw-compatible client can connect using the same `connect` + `chat.send` flow.

## Docker

All Docker-related files live in the `scripts/` directory.

### Image types

| File | Purpose | Base | User |
|------|---------|------|------|
| `scripts/Dockerfile` | **Production** — minimal runtime image with C++ binary + Sidecar | Ubuntu 22.04 multi-stage | `quantclaw` (non-root) |
| `scripts/Dockerfile.test` | **CI / Test** — runs C++ unit tests + Sidecar tests + E2E tests | Ubuntu 22.04 | root |
| `scripts/Dockerfile.dev` | **Development** — full toolchain + source + `gdb`/`valgrind`, interactive shell | Ubuntu 22.04 | root |

The production image uses a **three-stage build**: `cpp-builder` (compiles C++), `node-builder` (compiles TypeScript Sidecar), and `runtime` (copies only the final artifacts). It runs as a non-root user `quantclaw`.

### DOCKER_VERSION

`scripts/DOCKER_VERSION` is the single source of truth for the image version tag:

```bash
VERSION=$(cat scripts/DOCKER_VERSION)
# → 0.3.0-alpha
```

All three Compose services use this value via the `QUANTCLAW_VERSION` environment variable.
To set it explicitly before running Compose:

```bash
export QUANTCLAW_VERSION=$(cat scripts/DOCKER_VERSION)
```

### Quick start with Docker Compose

```bash
# Start the production gateway (detached)
docker compose -f scripts/docker-compose.yml up -d quantclaw

# View logs
docker compose -f scripts/docker-compose.yml logs -f quantclaw

# Run the full test suite in a one-shot container
docker compose -f scripts/docker-compose.yml run --rm quantclaw-test

# Start the dev container with a live source mount
docker compose -f scripts/docker-compose.yml run --rm quantclaw-dev
```

The compose file defines three services:

| Service | Image | Description |
|---------|-------|-------------|
| `quantclaw` | `quantclaw:VERSION` | Production gateway, restarts automatically |
| `quantclaw-test` | `quantclaw-test:VERSION` | One-shot test runner |
| `quantclaw-dev` | `quantclaw-dev:VERSION` | Dev shell with source volume mount |

### Build manually

```bash
VERSION=$(cat scripts/DOCKER_VERSION)

# Production image (with OCI labels)
docker build \
  -f scripts/Dockerfile \
  --build-arg VERSION=$VERSION \
  --build-arg BUILD_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ) \
  --build-arg VCS_REF=$(git rev-parse --short HEAD) \
  -t quantclaw:$VERSION \
  -t quantclaw:latest \
  .

# Test image
docker build -f scripts/Dockerfile.test -t quantclaw-test:$VERSION .

# Dev image
docker build -f scripts/Dockerfile.dev -t quantclaw-dev:$VERSION .
```

### Run the production image

```bash
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -e OPENAI_API_KEY=sk-... \
  -e ANTHROPIC_API_KEY=sk-ant-... \
  -e QUANTCLAW_LOG_LEVEL=info \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest
```

### Build args and environment variables

**Build args** (for the production image):

| Arg | Description |
|-----|-------------|
| `VERSION` | Written into OCI `org.opencontainers.image.version` label |
| `BUILD_DATE` | ISO-8601 build timestamp for the OCI label |
| `VCS_REF` | Git commit short SHA for the OCI label |
| `UBUNTU_VERSION` | Ubuntu base image version (default: `22.04`) |

**Runtime environment variables**:

| Variable | Default | Description |
|----------|---------|-------------|
| `OPENAI_API_KEY` | — | OpenAI / compatible provider API key |
| `ANTHROPIC_API_KEY` | — | Anthropic API key |
| `QUANTCLAW_LOG_LEVEL` | `info` | Log level: `debug` / `info` / `warn` / `error` |

### Volumes and ports

| Volume / Mount | Description |
|----------------|-------------|
| `/home/quantclaw/.quantclaw` | Config, workspace, sessions, and logs — **always persist this** |

| Port | Protocol | Description |
|------|----------|-------------|
| `18800` | WebSocket | Gateway RPC endpoint |
| `18801` | HTTP | Dashboard and REST API |

## Scripts

All helper scripts are in `scripts/`. Run them from the **repository root**.

| Script | Description |
|--------|-------------|
| `scripts/build.sh` | Smart build wrapper: color output, `-c` clean, `--debug`, `--tests`, `--asan`/`--tsan`/`--ubsan` sanitizers, CPU auto-detect, missing-dep auto-install. |
| `scripts/release.sh` | Build release tarball + SHA256 checksum. Reads version from `scripts/DOCKER_VERSION` or accepts an explicit version argument. Output goes to `dist/`. |
| `scripts/install.sh` | Native install: detects OS, installs system deps, builds from source, creates workspace. Run as root: `sudo bash scripts/install.sh` |
| `scripts/format-code.sh` | Format all C++ sources with `clang-format`. Pass `--check` for a dry-run (used in CI). |
| `scripts/format-code-docker.sh` | Same as above but runs inside Docker — no local `clang-format` required. |
| `scripts/build_ui.sh` | Build the web Dashboard UI assets. |

## Testing

### Unit Tests

```bash
cd build
./quantclaw_tests
# or
ctest --output-on-failure
```

### Smoke Tests (Integration)

The smoke test suite starts a real gateway process and exercises HTTP REST, WebSocket RPC, and concurrent connections — no API key required:

```bash
bash tests/smoke_test.sh
```

With an API key, agent conversation tests are also run:

```bash
OPENAI_API_KEY=sk-... bash tests/smoke_test.sh
```

Tests cover: lifecycle (health/status/auth), config RPCs, session RPCs, plugin RPCs, skill/cron/memory/queue/channel status, 10 concurrent WebSocket connections, and graceful shutdown. Logs are saved to `/tmp/quantclaw-smoke-ci/gateway.log`.

### Manual LLM Testing

```bash
# Start gateway
quantclaw gateway

# Non-streaming agent request
curl -X POST http://localhost:18801/api/agent/request \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"message": "Hello!", "sessionKey": "test:manual"}'

# OpenAI-compatible chat completion
curl -X POST http://localhost:18801/v1/chat/completions \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"model": "openai/qwen-max", "messages": [{"role": "user", "content": "Hi"}]}'
```

## Plugin Ecosystem

QuantClaw runs OpenClaw TypeScript plugins via a Node.js sidecar process. The C++ main process manages the sidecar lifecycle and communicates over **TCP loopback (127.0.0.1)** using JSON-RPC 2.0.

**Supported plugin capabilities**:
- **Tools**: Plugin-defined tools callable by the agent
- **Hooks**: 24 lifecycle hooks (void/modifying/sync modes)
- **Services**: Background services with start/stop management
- **Providers**: Custom LLM providers
- **Commands**: Slash commands exposed to the agent
- **HTTP Routes**: Plugin-defined HTTP endpoints via `/plugins/*`
- **Gateway Methods**: Plugin-defined RPC methods via `plugins.gateway`

**Plugin discovery** (in priority order):
1. Config-specified paths (`plugins.load.paths`)
2. Workspace plugins (`.openclaw/plugins/` or `.quantclaw/plugins/`)
3. Global plugins (`~/.quantclaw/plugins/`)
4. Bundled plugins (`~/.quantclaw/bundled-plugins/`)

Plugins use `openclaw.plugin.json` or `quantclaw.plugin.json` manifests, compatible with the OpenClaw plugin format.

```json
{
  "plugins": {
    "allow": ["my-plugin"],
    "deny": [],
    "entries": {
      "my-plugin": { "enabled": true, "config": {} }
    }
  }
}
```

### IPC Protocol (C++ Main Process ↔ Node.js Sidecar)

The IPC between the C++ host and the sidecar uses **TCP loopback**, which works identically on Linux and Windows:

**Connection setup**:
1. The C++ host binds to `127.0.0.1:0` — the OS assigns a free port.
2. The assigned port is forwarded to the sidecar child process via the `QUANTCLAW_PORT` environment variable.
3. The sidecar connects with Node.js's built-in `net.createConnection(port, '127.0.0.1')` — no extra npm packages needed.

**Frame format (NDJSON)**:

Each message is one JSON object followed by a newline `\n` ([Newline-Delimited JSON](https://ndjson.org/)):

```
{"jsonrpc":"2.0","method":"plugin.tools","params":{},"id":1}\n
{"jsonrpc":"2.0","result":[...],"id":1}\n
```

**Why `\n` never appears inside a JSON object**:

The JSON specification ([RFC 8259 §7](https://www.rfc-editor.org/rfc/rfc8259#section-7)) mandates that control characters (U+0000–U+001F, including newline U+000A) inside strings **must** be escaped as `\n` (backslash + letter n, 2 bytes) — never as raw byte `0x0A`.

- C++ side: `nlohmann::json::dump()` (no indent) produces compact JSON with all control characters auto-escaped.
- Node.js side: `JSON.stringify()` (no indent) gives the same guarantee.

The `\n` byte (`0x0A`) therefore **only** appears as a frame delimiter between messages, never inside a JSON payload. This is the same NDJSON framing used by Redis, Docker Events, and OpenAI streaming.

## OpenClaw Compatibility Status

QuantClaw aims for full compatibility with [OpenClaw](https://github.com/openclaw/openclaw) (v2026.2). The table below summarizes current alignment:

| Module | Status | Notes |
|--------|--------|-------|
| Workspace files | **Full** | All 8 files auto-created on onboard: `SOUL.md`, `MEMORY.md`, `SKILL.md`, `IDENTITY.md`, `HEARTBEAT.md`, `USER.md`, `AGENTS.md`, `TOOLS.md` |
| Skills format | **Full** | Both `metadata.openclaw` and flat formats |
| Plugin hooks (24 types) | **Full** | All 24 hook names and modes (void/modifying/sync) aligned |
| Plugin Sidecar IPC | **Full** | Tools, hooks, services, providers, commands, HTTP routes, gateway methods |
| JSONL session format | **Partial** | `message`, `thinking_level_change`, `custom_message` entry types implemented; `parentId` branching and write lock pending |
| Config format | **Partial** | JSON5 (comments, trailing commas) and `${VAR}` env substitution supported; `$include` directive pending |
| CLI commands | **Partial** | Core commands present (`gateway`, `agent`, `sessions`, `config`, `models`, `channels`, `plugins`, `health`, `status`, `run`, `eval`); missing `account`, `device` |
| Gateway RPC protocol | **Partial** | 57 methods implemented (~45% of OpenClaw surface); missing device pairing, node management, OAuth flows, extended cron/usage RPCs |
| Provider system | **Partial** | OpenAI + Anthropic fully implemented; Ollama + Gemini registered but stub; missing Mistral, Bedrock, Azure, Grok, Perplexity, LM Studio, Together, etc. (~12% of OpenClaw provider breadth) |
| Agent loop | **Partial** | Dynamic iterations (32–160), context guard, tool truncation, overflow compaction retry, budget pruning, subagent spawning all implemented; multi-stage compaction and `parentId` session branching pending |
| Memory search | **Partial** | BM25 keyword search only; missing hybrid vector search (embeddings, SQLite, MMR) |
| Context management | **Partial** | Budget-based compaction + pruning implemented; multi-stage (chunk + merge) pending |
| Channel system | **Partial** | External subprocess adapters; 0 built-in channels (OpenClaw has 38+); no 7-tier routing |
| Security / Sandbox | **Partial** | RBAC + rate limiter + `setrlimit` sandbox + exec approval; missing Docker sandbox, security audit framework |
| MCP | **Partial** | Tools + Resources + Prompts implemented (`tools/list`, `tools/call`, `resources/list`, `resources/read`, `prompts/list`, `prompts/get`); missing sampling API |
| Web API | **Partial** | 16 REST routes; missing OpenResponses API (`/v1/responses`), webhook endpoints |

### Key Differences from OpenClaw

| Aspect | OpenClaw | QuantClaw |
|--------|----------|-----------|
| Default gateway port | `18789` (WebSocket + HTTP) | `18800` (WebSocket), `18801` (HTTP) |
| Config format | JSON5 + `${VAR}` + `$include` | JSON5 + `${VAR}` (no `$include` yet) |
| Default model | `anthropic/claude-sonnet-4-6` | `anthropic/claude-sonnet-4-6` |
| Default maxTokens | `8192` | `4096` |
| Auth profiles | Multi-profile, OAuth + key rotation | Single API key per provider |
| Memory search | Hybrid (vector 0.7 + BM25 0.3) | BM25 only |
| Plugin execution | In-process (Node.js VM) | Out-of-process (TCP sidecar) |
| Channel adapters | 38+ built-in (Discord, Slack, Teams, Telegram, Matrix, IRC, etc.) | External subprocess scripts (user-provided) |

### QuantClaw-Only Features

| Feature | Description |
|---------|-------------|
| `chain` meta-tool | Declarative multi-step tool pipeline with `{{prev.result}}` templates |
| `read`/`write`/`edit` tools | Dedicated file operation tools with sandbox validation |
| Cross-platform TCP IPC | Unified Linux/Windows sidecar communication (no Unix sockets) |
| C++ resource limits | `setrlimit` sandbox (CPU/memory/fsize/nproc) |
| `viewer` RBAC role | Dedicated read-only role |

## Roadmap

Currently implemented: WebSocket/HTTP gateway, multi-provider LLM with failover, session persistence, plugin ecosystem (24 hook types, sidecar TCP IPC), channel subprocess adapters, MCP tools + resources + prompts, onboarding wizard (all 7 workspace files), JSON5 config, `${VAR}` env substitution, dynamic agent iterations (32–160), budget-based context management, subagent spawning, RBAC + exec approval sandbox, real browser CDP (WebSocket), `thinking_level_change` / `custom_message` JSONL entry types, `run` + `eval` + `plugins` CLI commands — **886 passing tests** (886 C++).

Not yet implemented:
- TUI interactive mode
- `account`, `device` CLI commands
- Config `$include` directive (modular config files)
- Multiple auth profiles with OAuth credential flows
- Session `parentId` branching (tree-shaped sessions)
- Hybrid memory search (vector embeddings + BM25, SQLite backend)
- Multi-stage context compaction (chunk + merge strategy)
- Built-in channel adapters (OpenClaw has 38+: Discord, Slack, Teams, Telegram, Matrix, etc.)
- Additional LLM providers (Gemini, Mistral, Bedrock, Azure, Grok, Perplexity, Ollama, etc.)
- MCP sampling API
- Docker sandbox isolation (per-session container)

## Troubleshooting

**Gateway won't start**
```bash
quantclaw config get gateway.port   # Check configured port
quantclaw doctor                    # Run diagnostics
```

**Can't connect to gateway**
```bash
quantclaw health    # Check if gateway is running
quantclaw logs -f   # Follow logs to diagnose errors (on Windows use: quantclaw logs -n 50)
quantclaw status    # Show connection and session counts
```

**API calls failing**
- Verify your LLM API key in `~/.quantclaw/quantclaw.json`
- Check `providers.openai.baseUrl` if using a custom endpoint
- Run `quantclaw doctor` for a full diagnostic report

**Config changes not taking effect**
```bash
quantclaw config reload   # Hot-reload without restarting the gateway
```

**Build failures**
- Ensure GCC 7+ or Clang 5+ with C++17 support
- Install system dependencies: `sudo apt install build-essential cmake libssl-dev libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev`
- Check CMake can find dependencies: `cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON`

## License

Apache License 2.0 — See [LICENSE](LICENSE) for details.

## Contributing

Contributions are welcome!

### Workflow

1. Fork the repository and clone locally
2. Create a feature branch: `git checkout -b feat/my-feature` or `fix/issue-123`
3. Make changes, add tests for new functionality
4. Format code: `./scripts/format-code.sh` (or use Docker: `./scripts/format-code-docker.sh`)
5. Run tests: `cd build && ctest --output-on-failure`
6. Commit and push, then open a Pull Request against `main`

### Code style

QuantClaw follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html), enforced with `clang-format`.

**VS Code** — add to `.vscode/settings.json`:

```json
{
    "C_Cpp.clang_format_style": "file",
    "editor.formatOnSave": true
}
```

**Pre-commit hook** (auto-formats before each commit):

```bash
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
./scripts/format-code.sh
git add -u
EOF
chmod +x .git/hooks/pre-commit
```

### Writing tests

Tests use [Google Test](https://github.com/google/googletest). Run a specific suite with:

```bash
./build/quantclaw_tests --gtest_filter=AgentLoopTest.*
```

Example test structure:

```cpp
#include <gtest/gtest.h>
#include "quantclaw/my_module.hpp"

TEST(MyModuleTest, BasicFunctionality) {
    MyModule module;
    EXPECT_TRUE(module.initialize());
    EXPECT_EQ(module.getValue(), 42);
}
```

### Commit message format

| Prefix | Purpose |
|--------|---------|
| `feat:` | New feature |
| `fix:` | Bug fix |
| `docs:` | Documentation changes |
| `refactor:` | Code refactoring |
| `test:` | Adding or updating tests |
| `chore:` | Build / tooling changes |

### Pull Request checklist

- All tests pass (`ctest --output-on-failure`)
- Code formatted with `clang-format` (CI checks this)
- No new compiler warnings
- README updated if adding user-facing features
- Unit tests added for new functionality

Questions? Open an [issue](https://github.com/QuantClaw/QuantClaw/issues) or start a [discussion](https://github.com/QuantClaw/QuantClaw/discussions).
