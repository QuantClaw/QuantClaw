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

配置文件路径：`~/.quantclaw/quantclaw.json`

```json
{
  "llm": {
    "model": "openai/qwen-max",
    "maxIterations": 15,
    "temperature": 0.7,
    "maxTokens": 4096
  },
  "providers": {
    "openai": {
      "apiKey": "YOUR_API_KEY",
      "baseUrl": "https://api.openai.com/v1"
    }
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
