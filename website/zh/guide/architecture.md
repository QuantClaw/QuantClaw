# 架构说明

QuantClaw 的架构专为性能、可靠性和可扩展性设计。

## 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                     用户界面                             │
│  CLI │ Web 仪表板 │ 聊天频道（Discord、Telegram）        │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│               网关（RPC 服务器）                         │
│  • HTTP / WebSocket API                                 │
│  • 命令路由                                             │
│  • 会话管理                                             │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│                  Agent 核心                              │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ 命令队列     │ 上下文管理器  │ 记忆管理器   │         │
│  └──────────────┴──────────────┴──────────────┘         │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ Provider     │ 工具注册表   │ Prompt 构建  │         │
│  │ 注册表       │              │ 器           │         │
│  └──────────────┴──────────────┴──────────────┘         │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│            插件系统（Node.js Sidecar）                   │
│  • 自定义技能                                           │
│  • Hook 和回调                                          │
│  • 频道适配器                                           │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│               外部集成                                   │
│  LLM 模型 │ Web 服务 │ 系统调用 │ 文件系统              │
└─────────────────────────────────────────────────────────┘
```

## 核心模块

### Agent 循环（`agent_loop.cpp`）

主事件处理引擎：

- **命令队列**：5 种操作模式（collect/followup/steer/interrupt/bypass）
- **全局并发**：线程安全的请求处理
- **错误恢复**：优雅降级和回退机制
- **动态迭代**：根据任务复杂度动态调整（32–160 次）

### 上下文管理器（`context_manager.cpp`）

智能对话上下文处理：

- **Token 计量**：追踪 Token 用量与限制
- **上下文裁剪**：基于 BM25 分数移除低相关内容
- **Overflow 处理**：自动压缩，支持 3 次重试
- **消息压缩**：归纳旧消息以保留历史

### 记忆管理器（`memory_manager.cpp`）

持久化记忆系统：

- **工作空间文件**：8 个 Markdown 文件（SOUL、MEMORY、SKILL 等）
- **BM25 搜索**：基于相关性的高效检索
- **文件管理**：工作空间文件操作
- **自动持久化**：跨会话保存/恢复

### Prompt 构建器（`prompt_builder.cpp`）

构建 LLM Prompt：

- **系统指令**：Agent 身份和能力
- **上下文集成**：记忆和对话历史
- **工具定义**：MCP Tool Schema
- **变量替换**：带验证的 `${VAR}` 展开

### 工具注册表（`tool_registry.cpp`）

管理可用工具：

- **内置工具**：bash、browser、process、web_search 等
- **MCP 集成**：工具定义和 Schema
- **执行安全**：权限和沙箱
- **动态加载**：运行时插件工具注册

### 会话管理器（`session_manager.cpp`）

管理对话会话：

- **会话生命周期**：创建、查询、更新、删除
- **历史管理**：消息持久化和检索（JSONL 格式）
- **多用户支持**：user-scoped 操作

## Provider 系统

### Provider 支持

| Provider | 状态 |
|----------|------|
| OpenAI（及兼容 API）| ✅ 完整 |
| Anthropic（Claude）| ✅ 完整 |
| Ollama | ⚠️ 已注册（Stub）|
| Gemini | ⚠️ 已注册（Stub）|
| Mistral、Bedrock、Azure、Grok 等 | ❌ 尚未实现 |

任何 OpenAI 兼容端点均可通过 `providers.openai.baseUrl` 接入（DeepSeek、通义千问、本地模型等）。

### 故障转移与弹性

- 每个 Provider 独立健康监控
- 失败时指数退避
- Profile 轮换
- 回退链执行

## 工具执行

### 执行流水线

1. **解析**：从 LLM 响应中提取 Tool Call
2. **验证**：检查 Schema 和权限
3. **执行**：沙箱内运行工具
4. **收集**：汇总结果
5. **截断**：限制结果大小以控制上下文
6. **格式化**：准备反馈给 LLM

### 内置工具

| 工具 | 用途 | 状态 |
|------|------|------|
| bash | 命令执行 | ✅ 可用 |
| browser | Chrome CDP 控制 | ✅ 可用 |
| web_search | 多 Provider 搜索 | ✅ 可用 |
| web_fetch | HTTP + HTML 解析 | ✅ 可用 |
| process | 后台进程管理 | ✅ 可用 |
| memory_search | BM25 检索 | ✅ 可用 |
| memory_get | 直接访问记忆 | ✅ 可用 |
| apply_patch | 代码补丁应用 | ✅ 可用 |

## 插件系统架构

### Sidecar 运行时

Node.js 子进程负责：
- 自定义技能
- Hook 执行
- 频道适配器
- 事件处理器

### IPC 通信

- **TCP 协议**：`127.0.0.1:QUANTCLAW_PORT`
- **JSON-RPC**：请求/响应消息
- **NDJSON 帧格式**：每条消息 = 一个 JSON 对象 + `\n`
- **错误处理**：Sidecar 崩溃后优雅恢复

### 插件清单

```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "entrypoint": "dist/index.js",
  "hooks": {
    "before_agent_loop": "onBeforeLoop",
    "on_tool_execute": "onToolExecute"
  }
}
```

## MCP（Model Context Protocol）

### 已实现能力

- `tools/list`、`tools/call` — 工具发现和调用
- `resources/list`、`resources/read` — 资源读取
- `prompts/list`、`prompts/get` — Prompt 模板
- 待实现：sampling API

## 网关（RPC 服务器）

### 端口

| 服务 | 端口 | 协议 |
|------|------|------|
| WebSocket RPC | `18800` | WebSocket |
| HTTP REST / 仪表板 | `18801` | HTTP |

### 连接流程

1. 客户端连接 → 服务端发送 `connect.challenge`（含 nonce）
2. 客户端回复 `connect.hello`（含认证 token）
3. 客户端发送 JSON-RPC 请求 → 服务端返回结果

## 安全架构

### RBAC

- Resource-based 访问控制
- Scope-based 权限（operator.read/write、viewer 等）
- 多层授权级别
- 审计日志

### 工具沙箱

- `setrlimit` 资源限制（CPU/内存/文件大小/进程数）
- 文件系统路径白名单/黑名单
- Exec approval 机制

## 部署模式

### 单机前台模式

```bash
quantclaw onboard --quick      # 初始化
quantclaw gateway              # 前台运行网关
quantclaw agent "你好！"       # 发送消息
```

### 网关 Daemon

```bash
quantclaw gateway install      # 安装为系统服务
quantclaw gateway start        # 启动
quantclaw agent "你好！"       # 通过网关连接
```

### Docker 容器

```bash
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -e OPENAI_API_KEY=sk-... \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest
```

### 生产（Docker Compose）

```bash
docker compose -f scripts/docker-compose.yml up -d quantclaw
```

---

**下一步**：[了解插件开发](/zh/guide/plugins) 或查看 [CLI 参考](/zh/guide/cli-reference)。
