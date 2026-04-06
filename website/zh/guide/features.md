# 核心特性

QuantClaw 将强大的 AI 能力与本地优先执行和健壮的错误处理相结合。

## 🧠 智能对话

多轮对话，全上下文感知：

- **上下文管理**：自动压缩和裁剪，始终在 Token 限制内
- **动态迭代**：根据任务复杂度动态调整（32–160 次）
- **会话历史**：以 JSONL 格式持久化对话记录
- **记忆集成**：自动从知识库中检索上下文

```bash
quantclaw agent "帮我分析这段代码"
```

## 💾 持久记忆系统

高级记忆管理：

### 工作空间文件

8 个 Markdown 文件，onboard 时自动创建：
- **SOUL.md** — 助手身份与价值观
- **MEMORY.md** — 长期记忆
- **SKILL.md** — 可用技能声明
- **IDENTITY.md** — 能力自述
- **HEARTBEAT.md** — 定时状态/Cron 日志
- **USER.md** — 用户画像与偏好
- **AGENTS.md** — 已知 Agent 列表
- **TOOLS.md** — 工具使用指引

### 记忆操作

```bash
quantclaw memory search "用户偏好"
quantclaw memory search "近期事件" --limit 10
quantclaw memory status
```

### 自动上下文裁剪

- BM25 相关性评分
- Overflow 压缩（最多 3 次重试）
- Budget-based 上下文管理

## 🌐 浏览器控制

真实 Chrome DevTools Protocol 集成：

- **页面导航**：加载和控制 URL
- **DOM 交互**：点击、输入、提交表单
- **JavaScript 执行**：运行自定义脚本
- **截图**：视觉验证
- **Cookie 管理**：会话持久化
- 无浏览器时优雅降级

浏览器控制是 Agent 内置工具，在对话中通过工具调用使用，而非直接 CLI 命令。

## 💻 系统集成

### 命令执行

`bash` 工具通过沙箱执行系统命令：
- `setrlimit` 资源限制（CPU/内存/文件大小）
- 路径白名单/黑名单
- Exec approval 机制

### 文件操作

工作空间文件直接存储在文件系统中：

```bash
# 工作空间文件位于
ls ~/.quantclaw/agents/main/workspace/
cat ~/.quantclaw/agents/main/workspace/MEMORY.md
```

### 代码补丁

`apply_patch` 工具支持 `*** Begin Patch` 格式的差异补丁，无需外部工具。

### 进程管理

`process` 工具用于管理后台进程。

## 🔌 插件生态

通过 Node.js Sidecar 扩展的架构：

### 内置工具

- **bash** — 沙箱命令执行
- **browser** — CDP 集成
- **web_search** — 多 Provider 搜索（Tavily 优先，DuckDuckGo 兜底）
- **web_fetch** — HTTP + HTML 解析（SSRF 防护）
- **memory_search** — BM25 知识检索
- **memory_get** — 直接记忆访问
- **apply_patch** — 代码补丁应用
- **process** — 后台进程管理

### 技能管理

```bash
quantclaw skills list              # 列出已加载技能
quantclaw skills install NAME      # 安装技能依赖
```

## 🔄 多 Provider LLM 支持

OpenAI 兼容和 Anthropic API，自动故障转移：

### 支持的 Provider

| Provider | 状态 |
|----------|------|
| OpenAI（及兼容 API）| ✅ 完整 |
| Anthropic（Claude）| ✅ 完整 |
| Ollama / Gemini | ⚠️ Stub |
| Mistral、Bedrock、Azure 等 | ❌ 尚未实现 |

任何 OpenAI 兼容端点可通过修改 `providers.openai.baseUrl` 接入（DeepSeek、通义千问、本地 Ollama 等）。

### Provider 配置

```json
{
  "llm": {
    "model": "anthropic/claude-sonnet-4-6",
    "maxIterations": 15,
    "maxTokens": 4096
  },
  "providers": {
    "anthropic": { "apiKey": "sk-ant-..." },
    "openai": { "apiKey": "sk-..." }
  }
}
```

### 自动故障转移

- 失败时指数退避
- Profile 轮换和健康监控
- 回退链执行
- Token 用量累计追踪

## 🛡️ 安全控制

生产级安全：

### RBAC

角色包括：`agent.admin`、`operator.read`、`operator.write`、`viewer`

RBAC 在网关层强制执行。

### 工具权限

- Per-tool 允许/拒绝规则
- 命令模式匹配
- 执行审批（exec approval）
- 审计日志

### 沙箱

- `setrlimit` 进程隔离（CPU/内存/文件大小/进程数）
- 文件系统路径限制
- 网络策略

## 📊 用量追踪

```bash
quantclaw sessions list       # 会话列表
quantclaw memory status       # 记忆索引统计
quantclaw logs               # 实时日志
quantclaw health             # 网关健康状态
```

## 💬 频道适配器

接入外部消息平台：

### 支持的频道

- **Discord** — 外部 subprocess 适配器（位于 `src/adapters/` 目录）
- **Telegram** — 外部 subprocess 适配器（位于 `src/adapters/` 目录）
- **飞书 / Lark** — 外部 subprocess 适配器（位于 `src/adapters/` 目录）
- **QQ** — 外部 subprocess 适配器，基于 QQ 官方机器人 API v2（位于 `src/adapters/` 目录）
- **Web 仪表板** — 内置，端口 18801

### 频道配置

```json
{
  "channels": {
    "discord": {
      "enabled": true,
      "token": "YOUR_DISCORD_BOT_TOKEN",
      "allowedIds": []
    },
    "telegram": {
      "enabled": false,
      "token": "YOUR_TELEGRAM_BOT_TOKEN",
      "allowedIds": []
    },
    "qq": {
      "enabled": false,
      "appId": "YOUR_QQ_BOT_APP_ID",
      "appSecret": "YOUR_QQ_BOT_APP_SECRET",
      "sandbox": false,
      "groupPolicy": "mention",
      "dmPolicy": "open",
      "allowedIds": []
    }
  }
}
```

## 🕐 定时任务

计划和触发自动化：

```bash
quantclaw cron add "daily-report" "0 9 * * *" "发送每日摘要"
quantclaw cron list
quantclaw cron remove TASK_ID
```

## 📊 性能优化

- **C++17 原生**：直接编译为目标平台二进制
- **高效内存**：上下文压缩和裁剪
- **快速启动**：模块懒初始化
- **并发处理**：多线程请求处理
- **流式响应**：实时 Token 推送

---

**下一步**：[了解架构](/zh/guide/architecture) 或[开始构建插件](/zh/guide/plugins)。
