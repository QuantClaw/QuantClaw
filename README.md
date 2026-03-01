<p align="center">
  <img src="assets/quantclaw-logo-transparent.png" alt="QuantClaw" width="180" />
</p>

<h1 align="center">QuantClaw</h1>

<p align="center">
  <strong>High-performance personal AI assistant in C++17</strong>
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

## Quick Start

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

## Architecture

```
~/.quantclaw/
├── quantclaw.json              # Configuration (OpenClaw format)
└── agents/default/
    ├── workspace/
    │   ├── SOUL.md             # Assistant identity
    │   ├── USER.md             # User information
    │   ├── MEMORY.md           # Long-term memory
    │   ├── memory/             # Daily memory logs
    │   │   └── YYYY-MM-DD.md
    │   └── skills/             # Skills (OpenClaw compatible)
    │       └── weather/
    │           └── SKILL.md
    └── sessions/
        ├── sessions.json       # Session index
        └── <session-id>.jsonl  # Per-session transcript
```

## Configuration

QuantClaw uses JSON configuration (`~/.quantclaw/quantclaw.json`):

```json
{
  "agent": {
    "model": "openai/qwen-max",
    "maxIterations": 15,
    "temperature": 0.7,
    "maxTokens": 4096,
    "fallbacks": ["anthropic/claude-sonnet-4-6"],
    "autoCompact": true,
    "compactMaxMessages": 100
  },
  "providers": {
    "openai": {
      "apiKey": "YOUR_API_KEY",
      "baseUrl": "https://api.openai.com/v1"
    }
  },
  "queue": {
    "maxConcurrent": 4,
    "debounceMs": 1000,
    "defaultMode": "collect"
  },
  "gateway": {
    "port": 18789,
    "bind": "loopback",
    "auth": { "mode": "token", "token": "YOUR_SECRET_TOKEN" },
    "controlUi": { "enabled": true, "port": 18790 }
  },
  "channels": {
    "discord": { "enabled": false, "token": "YOUR_DISCORD_TOKEN" },
    "telegram": { "enabled": false, "token": "YOUR_TELEGRAM_TOKEN" }
  },
  "tools": {
    "allow": ["group:fs", "group:runtime"],
    "deny": []
  },
  "security": {
    "sandbox": { "enabled": true }
  }
}
```

The model field uses `provider/model-name` prefix routing. If no prefix is given, it defaults to `openai`. See `config.example.json` for a full example with all options.

### Dependencies

**Required (system packages)**:
- C++17 compiler (GCC 7+, Clang 5+, MSVC 19.14+)
- spdlog — logging
- nlohmann/json — JSON library
- libcurl — HTTP client
- OpenSSL — TLS/SSL

**Fetched automatically by CMake**:
- IXWebSocket 11.4.5 — WebSocket server/client
- cpp-httplib 0.18.3 — HTTP server
- Google Test 1.14.0 — testing framework

## Usage

### Gateway (background service)

```bash
# Run gateway in foreground
quantclaw gateway

# Install as system service (systemd / launchd)
quantclaw gateway install

# Start / stop / restart daemon
quantclaw gateway start
quantclaw gateway stop
quantclaw gateway restart

# Check status
quantclaw gateway status
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

### Other commands

```bash
quantclaw health          # Quick health check
quantclaw config get      # View config
quantclaw skills list     # List loaded skills
quantclaw doctor          # Diagnostic check
```

## Channel Adapters

QuantClaw supports external channel adapters that connect to the gateway as standard WebSocket RPC clients. Adapters are Node.js processes managed by `ChannelAdapterManager`.

**Built-in adapters** (in `adapters/`):

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

When the gateway is running, the HTTP API is available at `http://localhost:18790`:

```bash
# Health check
curl http://localhost:18790/api/health

# Gateway status
curl http://localhost:18790/api/status

# Send a message (non-streaming)
curl -X POST http://localhost:18790/api/agent/request \
  -H "Content-Type: application/json" \
  -d '{"message": "Hello!", "sessionKey": "my:session"}'

# List sessions
curl http://localhost:18790/api/sessions?limit=10

# Session history
curl "http://localhost:18790/api/sessions/history?sessionKey=my:session"
```

With authentication enabled, add the `Authorization` header:
```bash
curl -H "Authorization: Bearer YOUR_TOKEN" http://localhost:18790/api/status
```

### Plugin API endpoints

```bash
# List loaded plugins
curl http://localhost:18790/api/plugins

# Get tool schemas from plugins
curl http://localhost:18790/api/plugins/tools

# Call a plugin tool
curl -X POST http://localhost:18790/api/plugins/tools/my-tool \
  -H "Content-Type: application/json" \
  -d '{"arg1": "value"}'

# List plugin services / providers / commands
curl http://localhost:18790/api/plugins/services
curl http://localhost:18790/api/plugins/providers
curl http://localhost:18790/api/plugins/commands
```

## WebSocket RPC Protocol (OpenClaw Compatible)

The gateway exposes a WebSocket RPC interface on port 18789:

1. Client connects → server sends `connect.challenge` with nonce
2. Client responds with `connect.hello` containing auth token
3. Client sends JSON-RPC requests → server responds with results

**Available RPC methods**: `gateway.health`, `gateway.status`, `config.get`, `agent.request`, `agent.stop`, `sessions.list`, `sessions.history`, `sessions.delete`, `sessions.reset`, `channels.list`, `chain.execute`, `plugins.list`, `plugins.tools`, `plugins.call_tool`, `plugins.services`, `plugins.providers`, `plugins.commands`, `plugins.gateway`, `queue.status`, `queue.configure`, `queue.cancel`, `queue.abort`

Streaming responses emit real-time events: `text_delta`, `tool_use`, `tool_result`, `message_end`.

Any OpenClaw-compatible client can connect using the same `connect` + `chat.send` flow.

## Docker

```bash
# Build and run
docker compose up -d

# Or build manually
docker build -t quantclaw .
docker run -d \
  -p 18789:18789 \
  -e OPENAI_API_KEY=your-key \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw
```

The Docker image uses a multi-stage build (Ubuntu 22.04) and runs as a non-root user. Configuration is persisted via the `/home/quantclaw/.quantclaw` volume.

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

## Compatibility

- **Workspace Files**: Compatible with OpenClaw (`SOUL.md`, `USER.md`, `MEMORY.md`)
- **Skills**: Uses OpenClaw SKILL.md format (supports both `metadata.openclaw` and flat formats)
- **Plugins**: Full OpenClaw plugin compatibility — tools, hooks, services, providers, commands
- **Configuration**: JSON format compatible with OpenClaw ecosystem
- **Protocol**: WebSocket RPC with `connect` + `chat.send` — interoperable with OpenClaw clients

## License

Apache License 2.0 — See [LICENSE](LICENSE) for details.

## Contributing

Contributions are welcome!

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Open a pull request
