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
- **模型故障转移**：多 API Key 轮换 + 指数退避冷却 + 自动模型回退链
- **命令队列**：per-session 串行保证，支持 collect/followup/steer/interrupt 模式和全局并发控制
- **上下文治理**：自动压缩、工具结果裁剪、BM25 记忆搜索
- **频道适配器**：接入 Discord、Telegram 或自定义机器人
- **会话持久化**：完整对话历史（含工具调用上下文）以 JSONL 格式保存
- **技能系统**：兼容 OpenClaw SKILL.md 格式（同时支持 OpenClaw 和 QuantClaw 两种清单格式）
- **插件生态**：通过 Node.js Sidecar 完全兼容 OpenClaw 插件——工具、钩子、服务、Provider、命令、HTTP 路由、网关方法
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

配置文件路径：`~/.quantclaw/quantclaw.json`

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

`model` 字段使用 `provider/model-name` 前缀路由。不带前缀时默认走 `openai`。任何兼容 OpenAI Chat Completion 格式的 API 都可以通过修改 `baseUrl` 接入（通义千问、DeepSeek、本地 Ollama 等）。

完整配置示例见 `config.example.json`。

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

### 插件 API 接口

```bash
# 列出已加载插件
curl http://localhost:18790/api/plugins

# 获取插件工具 Schema
curl http://localhost:18790/api/plugins/tools

# 调用插件工具
curl -X POST http://localhost:18790/api/plugins/tools/my-tool \
  -H "Content-Type: application/json" \
  -d '{"arg1": "value"}'

# 列出插件服务 / Provider / 命令
curl http://localhost:18790/api/plugins/services
curl http://localhost:18790/api/plugins/providers
curl http://localhost:18790/api/plugins/commands
```

## WebSocket RPC 协议（OpenClaw 兼容）

网关在端口 18789 暴露 WebSocket RPC 接口：

1. 客户端连接 → 服务端发送 `connect.challenge`（含 nonce）
2. 客户端回复 `connect.hello`（含认证 token）
3. 客户端发送 JSON-RPC 请求 → 服务端返回结果

**可用 RPC 方法**：`gateway.health`、`gateway.status`、`config.get`、`agent.request`、`agent.stop`、`sessions.list`、`sessions.history`、`sessions.delete`、`sessions.reset`、`channels.list`、`chain.execute`、`plugins.list`、`plugins.tools`、`plugins.call_tool`、`plugins.services`、`plugins.providers`、`plugins.commands`、`plugins.gateway`、`queue.status`、`queue.configure`、`queue.cancel`、`queue.abort`

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

## 插件生态

QuantClaw 通过 Node.js Sidecar 进程运行 OpenClaw TypeScript 插件。C++ 主进程管理 Sidecar 生命周期，通过 **TCP 本地回环（127.0.0.1）** 以 JSON-RPC 2.0 协议通信。

**支持的插件能力**：
- **工具（Tools）**：插件定义的工具，可被 Agent 调用
- **钩子（Hooks）**：24 种生命周期钩子（void/modifying/sync 三种模式）
- **服务（Services）**：后台服务，支持启动/停止管理
- **Provider**：自定义 LLM Provider
- **命令（Commands）**：暴露给 Agent 的斜杠命令
- **HTTP 路由**：通过 `/plugins/*` 暴露插件定义的 HTTP 接口
- **网关方法**：通过 `plugins.gateway` 暴露插件定义的 RPC 方法

**插件发现**（按优先级排列）：
1. 配置指定路径（`plugins.load.paths`）
2. 工作空间插件（`.openclaw/plugins/` 或 `.quantclaw/plugins/`）
3. 全局插件（`~/.quantclaw/plugins/`）
4. 内置插件（`~/.quantclaw/bundled-plugins/`）

插件使用 `openclaw.plugin.json` 或 `quantclaw.plugin.json` 清单文件，与 OpenClaw 插件格式兼容。

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

### IPC 通信协议（C++ 主进程 ↔ Node.js Sidecar）

C++ 主进程与 Sidecar 之间的进程间通信（IPC）采用 **TCP 本地回环**，适配 Linux 和 Windows 双平台：

**连接建立**：
1. C++ 主进程绑定 `127.0.0.1:0`，由操作系统分配空闲端口
2. 实际端口号通过 `QUANTCLAW_PORT` 环境变量传递给 Sidecar 子进程
3. Sidecar 用 Node.js 内置 `net.createConnection(port, '127.0.0.1')` 发起连接——无需额外 npm 依赖

**数据包格式（NDJSON）**：

每条消息 = 一个 JSON 对象 + 换行符 `\n`（即 [Newline-Delimited JSON](https://ndjson.org/)）：

```
{"jsonrpc":"2.0","method":"plugin.tools","params":{},"id":1}\n
{"jsonrpc":"2.0","result":[...],"id":1}\n
```

**为何 JSON 对象内部不会出现 `\n` 字符**：

JSON 规范（[RFC 8259 §7](https://www.rfc-editor.org/rfc/rfc8259#section-7)）强制要求字符串中的控制字符（码点 U+0000–U+001F，包含换行符 U+000A）必须转义为 `\n`（反斜杠 + 字母 n，2 字节），而非原始字节 `0x0A`（1 字节）。

因此：
- C++ 侧：`nlohmann::json::dump()`（无缩进参数）输出紧凑 JSON，所有控制字符自动转义
- Node.js 侧：`JSON.stringify()`（无缩进参数）同样保证此行为

换行符 `\n`（`0x0A`）**只会**出现在两条消息之间作为帧分隔符，绝不会出现在 JSON 内容中。这与 Redis RESP、Docker Events Stream、OpenAI Streaming 所采用的 NDJSON 实践完全一致。

## 兼容性

- **工作空间文件**：兼容 OpenClaw（`SOUL.md`、`USER.md`、`MEMORY.md`）
- **技能系统**：使用 OpenClaw SKILL.md 格式（支持 `metadata.openclaw` 嵌套格式和扁平格式）
- **插件系统**：完全兼容 OpenClaw 插件——工具、钩子、服务、Provider、命令
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
