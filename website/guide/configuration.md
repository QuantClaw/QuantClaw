# Configuration Guide

Configure QuantClaw for your specific use case.

## Configuration File

QuantClaw stores its configuration at `~/.quantclaw/quantclaw.json` (JSON5 format — comments and trailing commas are supported).

A full annotated example is available in `config.example.json` in the repository root.

## Configuration Structure

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

## LLM Configuration (`llm`)

| Key | Default | Description |
|-----|---------|-------------|
| `model` | `openai/qwen-max` | Default model in `provider/model-name` format |
| `maxIterations` | `15` | Maximum agent loop iterations per request |
| `temperature` | `0.7` | Sampling temperature (0.0–1.0) |
| `maxTokens` | `4096` | Maximum tokens for each LLM response |

The `model` field uses `provider/model-name` prefix routing. If no prefix is given, it defaults to `openai`. Any OpenAI-compatible API can be used by setting the appropriate `baseUrl` in `providers`:

```json
{
  "llm": {
    "model": "openai/qwen-max"
  },
  "providers": {
    "openai": {
      "apiKey": "YOUR_KEY",
      "baseUrl": "https://dashscope.aliyuncs.com/compatible-mode/v1"
    }
  }
}
```

## Provider Configuration (`providers`)

Each key under `providers` defines a named provider:

```json
{
  "providers": {
    "openai": {
      "apiKey": "sk-...",
      "baseUrl": "https://api.openai.com/v1",
      "timeout": 30
    },
    "anthropic": {
      "apiKey": "sk-ant-...",
      "baseUrl": "https://api.anthropic.com",
      "timeout": 30
    }
  }
}
```

**Options:**
- `apiKey`: API authentication key
- `baseUrl`: API base URL (change to use compatible endpoints like DeepSeek, local Ollama, etc.)
- `timeout`: Request timeout in seconds (default: `30`)

## Gateway Configuration (`gateway`)

```json
{
  "gateway": {
    "port": 18800,
    "bind": "loopback",
    "auth": {
      "mode": "token",
      "token": "YOUR_SECRET_TOKEN"
    },
    "controlUi": {
      "enabled": true,
      "port": 18801
    }
  }
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `port` | `18800` | WebSocket RPC gateway port |
| `bind` | `loopback` | Bind address: `loopback` (127.0.0.1) or `any` (0.0.0.0) |
| `auth.mode` | `token` | Auth mode: `token` or `none` |
| `auth.token` | — | Secret token for client authentication |
| `controlUi.enabled` | `true` | Enable the web dashboard |
| `controlUi.port` | `18801` | HTTP port for dashboard and REST API |

**Note:** QuantClaw uses ports `18800-18801` (different from OpenClaw's `18789-18790`), so both can run simultaneously.

## Channel Configuration (`channels`)

```json
{
  "channels": {
    "discord": {
      "enabled": true,
      "token": "YOUR_DISCORD_BOT_TOKEN",
      "allowedIds": ["123456789"]
    },
    "telegram": {
      "enabled": false,
      "token": "YOUR_TELEGRAM_BOT_TOKEN",
      "allowedIds": []
    }
  }
}
```

| Key | Description |
|-----|-------------|
| `enabled` | Enable/disable this channel adapter |
| `token` | Bot token from Discord/Telegram |
| `allowedIds` | Allowlist of user/group IDs (empty = allow all) |

## Tool Configuration (`tools`)

```json
{
  "tools": {
    "allow": ["group:fs", "group:runtime"],
    "deny": ["bash"]
  }
}
```

**Built-in tool groups:**
- `group:fs` — file read/write/edit/apply_patch
- `group:runtime` — bash, process, web_search, web_fetch, browser
- `group:memory` — memory_search, memory_get

## Security Configuration (`security`)

```json
{
  "security": {
    "sandbox": {
      "enabled": true,
      "allowedPaths": ["~/.quantclaw/agents/main/workspace"],
      "deniedPaths": ["/etc", "/sys", "/proc"]
    }
  }
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `sandbox.enabled` | `true` | Enable filesystem sandbox |
| `sandbox.allowedPaths` | `["~/.quantclaw/agents/main/workspace"]` | Paths the agent may read/write |
| `sandbox.deniedPaths` | `["/etc", "/sys", "/proc"]` | Paths always blocked |

## MCP Configuration (`mcp`)

```json
{
  "mcp": {
    "servers": [
      {
        "name": "my-server",
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
      }
    ]
  }
}
```

## System / Logging (`system`)

```json
{
  "system": {
    "logLevel": "info"
  }
}
```

**Log levels:** `trace`, `debug`, `info`, `warn`, `error`

Log files are stored at `~/.quantclaw/logs/`. The main log (`quantclaw.log`) is size-rotated automatically; the gateway service log (`gateway.log`) is time-pruned at startup.

## Environment Variable Substitution

Configuration supports `${VAR}` substitution from the shell environment:

```json
{
  "providers": {
    "openai": {
      "apiKey": "${OPENAI_API_KEY}"
    },
    "anthropic": {
      "apiKey": "${ANTHROPIC_API_KEY}"
    }
  }
}
```

## Configuration Commands

```bash
# View full config
quantclaw config get

# Get a specific value (dot-path)
quantclaw config get llm.model

# Change a value
quantclaw config set llm.model "anthropic/claude-sonnet-4-6"

# Remove a key
quantclaw config unset llm.temperature

# Validate syntax and structure
quantclaw config validate

# Show configuration schema
quantclaw config schema

# Hot-reload config (no gateway restart needed)
quantclaw config reload
```

## Common Setups

### Minimal (OpenAI-compatible)

```json
{
  "llm": { "model": "openai/gpt-4o" },
  "providers": {
    "openai": { "apiKey": "sk-..." }
  }
}
```

### Anthropic Claude

```json
{
  "llm": { "model": "anthropic/claude-sonnet-4-6" },
  "providers": {
    "anthropic": { "apiKey": "sk-ant-..." }
  }
}
```

### Local Ollama

```json
{
  "llm": { "model": "openai/llama3" },
  "providers": {
    "openai": {
      "apiKey": "ollama",
      "baseUrl": "http://localhost:11434/v1"
    }
  }
}
```

### DeepSeek / Qwen / Custom Endpoint

```json
{
  "llm": { "model": "openai/deepseek-chat" },
  "providers": {
    "openai": {
      "apiKey": "YOUR_DEEPSEEK_KEY",
      "baseUrl": "https://api.deepseek.com/v1"
    }
  }
}
```

---

**Next**: [View CLI reference](/guide/cli-reference) or [get started](/guide/getting-started).
