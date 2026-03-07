# 配置参考

## 配置文件

QuantClaw 的配置存储在 `~/.quantclaw/quantclaw.json`（JSON5 格式，支持注释和尾逗号）。

完整带注释示例见仓库根目录的 `config.example.json`。

## 完整配置结构

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

## LLM 配置（`llm`）

| 键 | 默认值 | 说明 |
|----|--------|------|
| `model` | `openai/qwen-max` | 默认模型，格式为 `provider/model-name` |
| `maxIterations` | `15` | 每次请求的最大 Agent 循环次数 |
| `temperature` | `0.7` | 采样温度（0.0–1.0）|
| `maxTokens` | `4096` | 每次 LLM 响应的最大 token 数 |

`model` 字段使用 `provider/model-name` 前缀路由，不带前缀时默认走 `openai`。任何 OpenAI 兼容的 API 都可以通过修改 `providers.openai.baseUrl` 接入：

```json
{
  "llm": { "model": "openai/qwen-max" },
  "providers": {
    "openai": {
      "apiKey": "YOUR_KEY",
      "baseUrl": "https://dashscope.aliyuncs.com/compatible-mode/v1"
    }
  }
}
```

## Provider 配置（`providers`）

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

- `apiKey`：API 认证密钥
- `baseUrl`：API 基础 URL（可替换为 DeepSeek、本地 Ollama 等兼容端点）
- `timeout`：请求超时（秒），默认 `30`

## 网关配置（`gateway`）

| 键 | 默认值 | 说明 |
|----|--------|------|
| `port` | `18800` | WebSocket RPC 网关端口 |
| `bind` | `loopback` | 绑定地址：`loopback`（127.0.0.1）或 `any`（0.0.0.0）|
| `auth.mode` | `token` | 认证方式：`token` 或 `none` |
| `auth.token` | — | 客户端认证密钥 |
| `controlUi.enabled` | `true` | 启用 Web 仪表板 |
| `controlUi.port` | `18801` | 仪表板和 REST API 的 HTTP 端口 |

**注意**：QuantClaw 使用 `18800-18801` 端口（不同于 OpenClaw 的 `18789-18790`），两者可同时运行。

## 频道配置（`channels`）

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

- `enabled`：启用/禁用该频道适配器
- `token`：Discord/Telegram Bot Token
- `allowedIds`：允许的用户/群组 ID 白名单（空 = 允许所有）

## 工具配置（`tools`）

```json
{
  "tools": {
    "allow": ["group:fs", "group:runtime"],
    "deny": ["bash"]
  }
}
```

**内置工具组：**
- `group:fs` — 文件读写/编辑/apply_patch
- `group:runtime` — bash、process、web_search、web_fetch、browser
- `group:memory` — memory_search、memory_get

## 安全配置（`security`）

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

## MCP 配置（`mcp`）

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

## 日志配置（`system`）

```json
{
  "system": {
    "logLevel": "info"
  }
}
```

**日志级别：** `trace`、`debug`、`info`、`warn`、`error`

日志文件存储在 `~/.quantclaw/logs/`。

## 环境变量替换

配置支持 `${VAR}` 从 shell 环境变量中替换：

```json
{
  "providers": {
    "openai": { "apiKey": "${OPENAI_API_KEY}" },
    "anthropic": { "apiKey": "${ANTHROPIC_API_KEY}" }
  }
}
```

## 配置命令

```bash
quantclaw config get                    # 查看完整配置
quantclaw config get llm.model         # 查看指定配置项（点路径）
quantclaw config set llm.model "anthropic/claude-sonnet-4-6"
quantclaw config unset llm.temperature
quantclaw config validate              # 验证语法和结构
quantclaw config schema                # 查看配置 Schema
quantclaw config reload                # 热重载（无需重启网关）
```

## 常见配置示例

### 最小配置（OpenAI 兼容）

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

### 本地 Ollama

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

### DeepSeek / 通义千问 / 自定义端点

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

**下一步**：[查看 CLI 参考](/zh/guide/cli-reference) 或[快速开始](/zh/guide/getting-started)。
