<p align="center">
  <img src="assets/quantclaw-logo-transparent.png" alt="QuantClaw" width="180" />
</p>

<h1 align="center">QuantClaw</h1>

<p align="center">
  <strong>C++17 高性能私人 AI 助手</strong>
</p>

<p align="center">
  <a href="README.md">English</a>
</p>

---

QuantClaw 是 [OpenClaw](https://github.com/openclaw/openclaw) 生态的 C++ 原生实现，专注于性能和低内存占用，同时完全兼容 OpenClaw 的工作空间文件、技能系统和 WebSocket RPC 协议。

## 特性

- **原生性能**：C++17 编译为原生二进制，无解释器开销，无 GC 停顿
- **内存高效**：内存占用极低，适合资源受限的服务器环境
- **OpenClaw 兼容**：兼容 OpenClaw 工作空间文件、技能和配置格式
- **双协议接入**：WebSocket RPC 网关 + HTTP REST API
- **多模型支持**：OpenAI 兼容接口和 Anthropic API，通过 `provider/model` 前缀路由
- **频道适配器**：接入 Discord、Telegram 或自定义机器人
- **会话持久化**：完整对话历史（含工具调用上下文）以 JSONL 格式保存
- **技能系统**：兼容 OpenClaw SKILL.md 格式
- **MCP 支持**：Model Context Protocol，接入外部工具服务器
- **文件系统优先**：无数据库依赖，所有数据存储在工作空间目录

## 快速开始

```bash
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
mkdir build && cd build
cmake ..
make -j$(nproc)

# 运行测试
./quantclaw_tests

# 安装（可选）
sudo make install
```

## 架构

```
~/.quantclaw/
├── quantclaw.json              # 配置文件（OpenClaw 格式）
└── agents/default/
    ├── workspace/
    │   ├── SOUL.md             # 助手身份定义
    │   ├── USER.md             # 用户信息
    │   ├── MEMORY.md           # 长期记忆
    │   ├── memory/             # 每日记忆日志
    │   │   └── YYYY-MM-DD.md
    │   └── skills/             # 技能目录（OpenClaw 兼容）
    │       └── weather/
    │           └── SKILL.md
    └── sessions/
        ├── sessions.json       # 会话索引
        └── <session-id>.jsonl  # 单会话记录
```

## 配置

默认配置文件路径：`~/.quantclaw/quantclaw.json`。可通过 `--config` / `-c` 参数指定自定义配置文件：

```bash
quantclaw --config /path/to/config.json gateway
quantclaw -c /path/to/config.json gateway    # 简写形式
```

快速开始配置：

```bash
mkdir -p ~/.quantclaw
cp config.example.json ~/.quantclaw/quantclaw.json
# 编辑文件，填入你的 API Key
```

完整示例见 `config.example.json`。以下是各配置项的详细说明。

> **热重载**：网关每 5 秒检测配置文件变化并自动重载——大部分修改无需重启。切换 Provider（如 OpenAI → Anthropic）需要重启网关。

### `system` — 系统设置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `logLevel` | string | `"info"` | 日志详细程度：`trace` > `debug` > `info` > `warn` > `error` |

排查问题时设为 `debug`，生产环境用 `warn`。

```json
{ "system": { "logLevel": "info" } }
```

### `llm` — LLM 模型设置

这是最重要的配置——决定 Agent 使用哪个 AI 模型。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `model` | string | `"qwen-max"` | 模型标识，`provider/model` 格式（详见下方） |
| `maxIterations` | int | `15` | 每条用户消息的最大工具调用轮数 |
| `temperature` | float | `0.7` | 采样随机性（0 = 确定性输出，越高越有创造性） |
| `maxTokens` | int | `4096` | LLM 单次回复最大 token 数（包含文本 + 工具调用） |

**Provider/model 路由** — `model` 字段使用 `provider/model-name` 前缀，一个字符串同时选择 LLM 提供商和模型：

| `model` 值 | 使用的 Provider | 实际发送给 API 的模型名 |
|------------|----------------|----------------------|
| `"openai/gpt-4o"` | openai | `gpt-4o` |
| `"anthropic/claude-sonnet-4-20250514"` | anthropic | `claude-sonnet-4-20250514` |
| `"openai/qwen-max"` | openai | `qwen-max`（通过自定义 baseUrl） |
| `"qwen-max"` | openai（默认） | `qwen-max` |

不带前缀时默认使用 `openai` provider。任何兼容 OpenAI Chat Completion 格式的 API 都可以通过修改 provider 的 `baseUrl` 接入——通义千问、DeepSeek、Moonshot、本地 Ollama、vLLM 等均可。

**`maxIterations`** — 每个"迭代"是一次 LLM 调用，可能触发工具使用（读文件、执行命令等）。Agent 循环：调用 LLM → 执行工具 → 将结果回传 → 再次调用 LLM，直到达到此上限。达到上限后 Agent 停止并返回已有结果。复杂任务可设高（如 30），控制成本可设低（如 5）。

**`temperature`** — 控制随机性。`0.7` 是较好的平衡值。确定性/事实性任务用 `0.0`–`0.3`，创造性任务用 `0.7`–`1.0`。超过 `1.0` 很少有用。

**`maxTokens`** — 单次回复的 token 预算。如果模型达到此限制，回复会被截断。`4096` 适合大多数任务，仅在回复被截断时增加到 `8192`+。

```json
{
  "llm": {
    "model": "openai/qwen-max",
    "maxIterations": 15,
    "temperature": 0.7,
    "maxTokens": 4096
  }
}
```

### `providers` — LLM 提供商凭证

定义每个提供商的 API 凭证。key 名称（如 `"openai"`）对应 `llm.model` 中的 `provider/model` 前缀。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `apiKey` | string | `""` | API 密钥 |
| `baseUrl` | string | — | API 端点地址 |
| `timeout` | int | `30` | HTTP 请求超时（秒） |

**`apiKey`** — 你的密钥。OpenAI 以 `sk-` 开头，Anthropic 以 `sk-ant-` 开头。**切勿提交到 git**——建议使用环境变量。

**`baseUrl`** — API 端点地址。修改此值即可接入不同服务：

| 服务 | `baseUrl` |
|------|-----------|
| OpenAI 官方 | `https://api.openai.com/v1` |
| Anthropic 官方 | `https://api.anthropic.com` |
| 通义千问（Qwen） | `https://dashscope.aliyuncs.com/compatible-mode/v1` |
| DeepSeek | `https://api.deepseek.com/v1` |
| Moonshot（Kimi） | `https://api.moonshot.cn/v1` |
| 本地 Ollama | `http://localhost:11434/v1` |
| 本地 vLLM | `http://localhost:8000/v1` |

**`timeout`** — 等待 LLM 响应的时间。`30` 秒适合大多数 API。网络慢或响应长可增加到 `60`–`120`。

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

<details>
<summary><b>示例：使用通义千问（Qwen）</b></summary>

```json
{
  "llm": { "model": "openai/qwen-max" },
  "providers": {
    "openai": {
      "apiKey": "sk-你的dashscope密钥",
      "baseUrl": "https://dashscope.aliyuncs.com/compatible-mode/v1"
    }
  }
}
```
</details>

<details>
<summary><b>示例：使用本地 Ollama</b></summary>

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
</details>

<details>
<summary><b>示例：使用 Anthropic Claude</b></summary>

```json
{
  "llm": { "model": "anthropic/claude-sonnet-4-20250514" },
  "providers": {
    "anthropic": {
      "apiKey": "sk-ant-...",
      "baseUrl": "https://api.anthropic.com"
    }
  }
}
```
</details>

### `gateway` — WebSocket RPC 网关

网关是核心后台服务。它暴露**两个端口**：WebSocket RPC 端口用于程序化客户端，HTTP 端口用于 REST API / 控制面板。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `port` | int | `18789` | WebSocket RPC 监听端口 |
| `bind` | string | `"loopback"` | 网络绑定模式 |
| `auth.mode` | string | `"token"` | 认证模式 |
| `auth.token` | string | `""` | 客户端认证密钥 |
| `controlUi.enabled` | bool | `true` | 是否启用 HTTP REST API / 控制面板 |
| `controlUi.port` | int | `18790` | HTTP API 监听端口 |

**`bind`** — 控制谁可以连接网关：
- `"loopback"` — 仅本机进程（127.0.0.1）。**推荐大多数用户使用**。远程访问请用 SSH 隧道。
- `"all"` — 所有网络接口（0.0.0.0）。必须同时启用认证并配置防火墙。

**`auth.mode`** 和 **`auth.token`**：
- `"token"` — 客户端必须提供密钥才能连接。环境变量 `QUANTCLAW_AUTH_TOKEN` 优先级高于配置文件。
- `"none"` — 不认证。仅在 `bind` 为 `"loopback"` 时安全。

**`controlUi`** — 启用后，在 `controlUi.port` 上启动 HTTP 服务，提供：
- REST API 端点（`/api/health`、`/api/status`、`/api/agent/request` 等）
- 仪表盘 UI（如果安装在 `~/.quantclaw/ui/`）
- Gateway 信息端点，供 UI 自动发现 WebSocket 端口

> **注意**：`port`（18789）和 `controlUi.port`（18790）必须不同。WebSocket 端口给 RPC 客户端用，HTTP 端口给浏览器和 `curl` 用。

```json
{
  "gateway": {
    "port": 18789,
    "bind": "loopback",
    "auth": { "mode": "token", "token": "YOUR_SECRET_TOKEN" },
    "controlUi": { "enabled": true, "port": 18790 }
  }
}
```

### `channels` — IM 频道适配器

将 Agent 接入即时通讯平台。每个 key 是频道名称（如 `"discord"`、`"telegram"`）。网关启动时会将已启用的频道作为 Node.js 子进程拉起，子进程通过 WebSocket RPC 连回网关。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | bool | `false` | 是否在网关启动时拉起该适配器 |
| `token` | string | `""` | 平台的 Bot Token |
| `allowedIds` | string[] | `[]` | 允许交互的用户/群组 ID（空数组 = 允许所有人） |

**`enabled`** — 设为 `true` 即激活。适配器进程仅在此值为 `true` 时启动；设为 `false` 可禁用而无需删除配置。

**`token`** — 从平台获取的 Bot Token：
- Discord：[Discord 开发者门户](https://discord.com/developers/applications) → Bot → Token
- Telegram：[@BotFather](https://t.me/BotFather) → `/newbot` → Token

**`allowedIds`** — 限制谁可以和 Bot 对话。留空 `[]` 则所有人可用。填入用户 ID 或群组/频道 ID 进行白名单限制。

适配器进程会收到完整的频道配置（通过 `QUANTCLAW_CHANNEL_CONFIG` 环境变量），因此你添加的任何平台特定字段（如 `clientId`、`groupPolicy`）都会透传给适配器。

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

### `tools` — 工具权限控制

控制 Agent 可以使用哪些内置工具。Agent 拥有读写文件、执行命令、发送消息等工具。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `allow` | string[] | `["group:fs", "group:runtime"]` | Agent **可以**使用的工具组或工具名 |
| `deny` | string[] | `[]` | Agent **不能**使用的工具组或工具名（**优先级高于** allow） |

**内置工具组**：

| 工具组 | 包含的工具 | 作用 |
|--------|-----------|------|
| `group:fs` | `read_file`、`write_file`、`edit_file` | 读取、创建、编辑工作空间中的文件 |
| `group:runtime` | `exec`、`message` | 执行 Shell 命令、发送消息 |
| `group:all` | 以上全部 | 所有工具 |

也可以按工具名单独允许/禁止（如 `"read_file"`、`"exec"`），或用 `mcp:` 前缀控制 MCP 工具（如 `"mcp:my-server:*"` 允许某服务器的所有工具，`"mcp:*"` 允许所有 MCP 工具）。

**`deny` 优先级更高** — 同时出现在 `allow` 和 `deny` 中的工具会被禁止。这允许你允许一组工具但排除特定项：

```json
{
  "tools": {
    "allow": ["group:fs", "group:runtime"],
    "deny": ["exec"]
  }
}
```
以上配置允许文件操作但禁止执行 Shell 命令。

### `security` — 安全沙箱

沙箱限制 Agent 工具可以访问的文件系统路径，并对子进程施加资源限制。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `sandbox.enabled` | bool | `true` | 是否启用文件系统沙箱 |
| `sandbox.allowedPaths` | string[] | `[]` | Agent **可以**访问的路径（空 = 不限制） |
| `sandbox.deniedPaths` | string[] | `[]` | Agent **不能**访问的路径（优先检查，覆盖 allow） |

**路径检查逻辑**：
1. 先检查 `deniedPaths` — 匹配则**立即拒绝**。
2. 如果 `allowedPaths` 非空，路径必须匹配其中之一；如果为空，则允许所有路径（被拒绝的除外）。
3. 路径按前缀匹配：`/home/user` 同时允许 `/home/user/foo/bar.txt`。

**资源限制**（仅 Linux，应用于子进程）：
- CPU：软限 30 秒 / 硬限 60 秒
- 内存：软限 256 MB / 硬限 512 MB
- 文件大小：软限 64 MB / 硬限 128 MB

**危险命令拦截**：沙箱还会拦截已知的破坏性命令模式，如 `rm -rf /`、`mkfs`、原始 `dd` 写入。

```json
{
  "security": {
    "sandbox": {
      "enabled": true,
      "allowedPaths": ["~/.quantclaw/agents/default/workspace"],
      "deniedPaths": ["/etc", "/sys", "/proc"]
    }
  }
}
```

### `mcp` — Model Context Protocol

[MCP](https://modelcontextprotocol.io/) 让你通过外部工具服务器扩展 Agent 能力。每个 MCP 服务器提供额外的工具（如数据库查询、网页抓取、自定义 API），Agent 可以直接调用。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `servers` | object[] | `[]` | MCP 服务器连接列表 |
| `servers[].name` | string | `""` | 服务器标识（用于工具命名） |
| `servers[].url` | string | `""` | MCP 服务器的 HTTP 端点 |
| `servers[].timeout` | int | `30` | 请求超时（秒） |

**工作原理**：启动时，网关连接每个 MCP 服务器，通过 `list_tools` 发现可用工具，并注册到 Agent 的工具列表中。工具名以 `mcp__{server-name}__{tool-name}` 格式命名以避免冲突。

当 Agent 决定使用某个 MCP 工具时，网关向对应服务器发送 `call_tool` JSON-RPC 请求并返回结果。

```json
{
  "mcp": {
    "servers": [
      { "name": "my-tools", "url": "http://localhost:3001", "timeout": 30 }
    ]
  }
}
```

### 最小快速启动配置

如果你只想尽快跑起来，以下是最精简的配置：

```json
{
  "llm": {
    "model": "openai/qwen-max"
  },
  "providers": {
    "openai": {
      "apiKey": "YOUR_API_KEY",
      "baseUrl": "https://dashscope.aliyuncs.com/compatible-mode/v1"
    }
  }
}
```

其他所有配置项均使用合理的默认值（网关端口 18789、HTTP API 端口 18790、仅本机绑定、沙箱启用）。

### 依赖

**系统包（需手动安装）**：
- C++17 编译器（GCC 7+、Clang 5+、MSVC 19.14+）
- spdlog — 日志
- nlohmann/json — JSON 库
- libcurl — HTTP 客户端
- OpenSSL — TLS/SSL

**CMake 自动拉取**：
- IXWebSocket 11.4.5 — WebSocket 服务端/客户端
- cpp-httplib 0.18.3 — HTTP 服务端
- Google Test 1.14.0 — 测试框架

### Ubuntu / Debian 一键安装依赖

```bash
sudo apt install build-essential cmake libssl-dev \
  libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev zlib1g-dev
```

## 使用

### 网关（后台服务）

```bash
# 前台运行
quantclaw gateway

# 安装为系统服务（systemd / launchd）
quantclaw gateway install

# 启动 / 停止 / 重启
quantclaw gateway start
quantclaw gateway stop
quantclaw gateway restart

# 查看状态
quantclaw gateway status
```

### 与 AI 对话

```bash
# 发送消息
quantclaw agent "你好，介绍一下你自己"

# 指定会话
quantclaw agent --session my:session "今天天气怎么样？"
```

### 会话管理

```bash
quantclaw sessions list
quantclaw sessions history <session-key>
quantclaw sessions delete <session-key>
quantclaw sessions reset <session-key>
```

### 其他命令

```bash
quantclaw health          # 健康检查
quantclaw config get      # 查看配置
quantclaw skills list     # 列出已加载技能
quantclaw doctor          # 诊断检查
```

### 全局参数

```bash
quantclaw --config <路径> <命令>   # 使用自定义配置文件
quantclaw -c <路径> <命令>         # 简写形式
quantclaw --version                # 打印版本号
quantclaw --help                   # 显示帮助
quantclaw <命令> --json            # JSON 输出模式
```

## 频道适配器

QuantClaw 通过频道适配器接入外部消息平台。适配器是独立的 Node.js 进程，以标准 WebSocket RPC 客户端的方式连接网关。

**内置适配器**（`adapters/` 目录）：

| 适配器    | 依赖库      | 状态 |
|----------|-------------|------|
| Discord  | discord.js  | 可用 |
| Telegram | telegraf    | 可用 |

在配置中启用频道：

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

网关启动时会自动拉起已启用的适配器。每个适配器通过 `connect` + `chat.send` RPC 调用接入——和任何 OpenClaw 兼容客户端的接入方式完全一致。

## HTTP REST API

网关运行后，HTTP API 在 `http://localhost:18790` 可用：

```bash
# 健康检查
curl http://localhost:18790/api/health

# 网关状态
curl http://localhost:18790/api/status

# 发送消息（非流式）
curl -X POST http://localhost:18790/api/agent/request \
  -H "Content-Type: application/json" \
  -d '{"message": "你好！", "sessionKey": "my:session"}'

# 列出会话
curl http://localhost:18790/api/sessions?limit=10

# 查看会话历史
curl "http://localhost:18790/api/sessions/history?sessionKey=my:session"
```

启用认证时，需添加 `Authorization` 头：
```bash
curl -H "Authorization: Bearer YOUR_TOKEN" http://localhost:18790/api/status
```

## WebSocket RPC 协议（OpenClaw 兼容）

网关在端口 18789 暴露 WebSocket RPC 接口：

1. 客户端连接 → 服务端发送 `connect.challenge`（含 nonce）
2. 客户端回复 `connect.hello`（含认证 token）
3. 客户端发送 JSON-RPC 请求 → 服务端返回结果

**可用 RPC 方法**：`gateway.health`、`gateway.status`、`config.get`、`agent.request`、`agent.stop`、`sessions.list`、`sessions.history`、`sessions.delete`、`sessions.reset`、`channels.list`、`chain.execute`

流式响应会实时推送事件：`text_delta`、`tool_use`、`tool_result`、`message_end`。

任何 OpenClaw 兼容客户端都可以通过相同的 `connect` + `chat.send` 流程接入。

## Docker 部署

```bash
# 一键启动
docker compose up -d

# 或手动构建
docker build -t quantclaw .
docker run -d \
  -p 18789:18789 \
  -e OPENAI_API_KEY=your-key \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw
```

Docker 镜像使用多阶段构建（基于 Ubuntu 22.04），以非 root 用户运行。配置数据通过 `/home/quantclaw/.quantclaw` 卷持久化。

## Systemd 服务

项目提供了预配置的 systemd 服务文件 `systemd/quantclaw.service`，用于在 Linux 上将 QuantClaw 作为后台服务运行。

```bash
# 复制服务文件
sudo cp systemd/quantclaw.service /etc/systemd/system/

# 创建专用用户（可选，推荐）
sudo useradd -r -s /bin/false quantclaw

# 重载 systemd 并启用服务
sudo systemctl daemon-reload
sudo systemctl enable quantclaw
sudo systemctl start quantclaw

# 查看状态
sudo systemctl status quantclaw
```

服务文件已包含安全加固配置（`NoNewPrivileges`、`ProtectSystem=strict` 等）和资源限制。可根据需要编辑 `ExecStart`、环境变量或工作目录。

## 兼容性

- **工作空间文件**：兼容 OpenClaw（`SOUL.md`、`USER.md`、`MEMORY.md`）
- **技能系统**：使用 OpenClaw SKILL.md 格式
- **配置格式**：JSON 格式，兼容 OpenClaw 生态
- **协议**：WebSocket RPC，`connect` + `chat.send` 流程，可与 OpenClaw 客户端互通

## 许可证

Apache License 2.0 — 详见 [LICENSE](LICENSE)。

## 贡献

欢迎贡献！

1. Fork 本仓库
2. 创建功能分支
3. 提交更改
4. 推送分支
5. 发起 Pull Request
