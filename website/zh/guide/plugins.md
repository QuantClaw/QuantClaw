# 插件开发指南

学习如何创建自定义技能，通过插件扩展 QuantClaw 的功能。

## 概述

QuantClaw 的插件系统通过 Node.js Sidecar 运行，允许你：
- 创建自定义**工具**（Agent 可调用）
- 实现**钩子（Hooks）**处理生命周期事件
- 构建**频道适配器**接入新的聊天平台
- 注册自定义 **Provider**、**命令**、**HTTP 路由**、**网关方法**

插件在 Node.js Sidecar 子进程中运行，通过 TCP loopback（`127.0.0.1:QUANTCLAW_PORT`）与 C++ 主进程 JSON-RPC 通信。

## 创建第一个插件

### 插件目录结构

```
my-plugin/
├── openclaw.plugin.json   # 插件清单（也可用 quantclaw.plugin.json）
├── src/
│   └── index.ts           # 主入口
├── dist/
│   └── index.js           # 编译产物
└── package.json
```

### 插件清单（`openclaw.plugin.json`）

```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "description": "A custom QuantClaw plugin",
  "author": "Your Name",
  "entrypoint": "dist/index.js"
}
```

### 主入口（`src/index.ts`）

```typescript
import { PluginContext } from '@openclaw/sdk'

export async function activate(ctx: PluginContext) {
  // 注册工具
  ctx.tools.register({
    name: 'my_tool',
    description: 'What this tool does',
    parameters: {
      type: 'object',
      properties: {
        input: { type: 'string', description: 'Input text' }
      },
      required: ['input']
    },
    handler: async (params) => {
      return { result: `Processed: ${params.input}` }
    }
  })

  // 注册 Hook
  ctx.hooks.on('before_agent_loop', async (event) => {
    console.log('Agent loop starting...')
  })
}
```

## Hook 系统

QuantClaw 支持 **24 种生命周期 Hook**，分三种模式：

- **void**：并行执行，无返回值
- **modifying**：串行执行，可修改数据
- **sync**：同步执行，可阻塞流程

### 可用 Hook（部分）

| Hook 名称 | 模式 | 触发时机 |
|-----------|------|----------|
| `before_agent_loop` | void | Agent 循环开始前 |
| `after_agent_loop` | void | Agent 循环结束后 |
| `before_tool_execute` | modifying | 工具执行前 |
| `after_tool_result` | modifying | 工具结果返回后 |
| `on_message_received` | void | 收到用户消息时 |
| `on_error` | void | 发生错误时 |

## 技能（Skills）

技能是 SKILL.md 格式的轻量化插件，无需 Node.js 代码，适合注入 Prompt 内容：

```yaml
# ~/.quantclaw/skills/my-skill/SKILL.md
---
name: my-skill
emoji: "🔧"
description: 技能描述
requires:
  bins:
    - required-binary
  env:
    - REQUIRED_ENV_VAR
always: false
metadata:
  openclaw:
    install:
      apt: package-name
      node: npm-package
---

此处的 Markdown 指令会在技能激活时注入到 Agent 的系统 Prompt 中。
```

### 技能存储位置

- 全局技能：`~/.quantclaw/skills/`（与 Architecture 中一致）
- 工作空间技能：`~/.quantclaw/agents/main/workspace/skills/`

### 管理技能

```bash
quantclaw skills list              # 列出已加载技能
quantclaw skills install NAME      # 安装技能的 npm 依赖
```

## 插件配置

在 `~/.quantclaw/quantclaw.json` 中启用插件：

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

## 插件发现顺序

1. 配置指定路径（`plugins.load.paths`）
2. 工作空间插件（`.openclaw/plugins/` 或 `.quantclaw/plugins/`）
3. 全局插件（`~/.quantclaw/plugins/`）
4. 内置插件（`~/.quantclaw/bundled-plugins/`）

## 完整插件能力

| 能力 | 说明 |
|------|------|
| **工具（Tools）** | Agent 可调用的自定义工具 |
| **钩子（Hooks）** | 24 种生命周期事件（void/modifying/sync）|
| **服务（Services）** | 后台服务，支持启动/停止管理 |
| **Provider** | 自定义 LLM Provider |
| **命令（Commands）** | 暴露给 Agent 的斜杠命令 |
| **HTTP 路由** | 通过 `/plugins/*` 暴露 HTTP 接口 |
| **网关方法** | 通过 `plugins.gateway` 暴露 RPC 方法 |

## IPC 通信协议

C++ 主进程与 Sidecar 之间采用 **NDJSON over TCP loopback**：

```
{"jsonrpc":"2.0","method":"plugin.tools","params":{},"id":1}\n
{"jsonrpc":"2.0","result":[...],"id":1}\n
```

**连接建立：**
1. C++ 主进程绑定 `127.0.0.1:0`，OS 分配空闲端口
2. 端口通过 `QUANTCLAW_PORT` 环境变量传给 Sidecar
3. Sidecar 用 `net.createConnection(port, '127.0.0.1')` 连接——无需额外 npm 包

## OpenClaw 兼容性

QuantClaw 插件与 OpenClaw 插件格式完全兼容：
- 使用 `openclaw.plugin.json` 或 `quantclaw.plugin.json` 清单
- 支持所有 24 种 Hook 类型和三种模式
- 现有 OpenClaw 插件无需修改即可在 QuantClaw 中运行

---

**下一步**：[从源码构建](/zh/guide/building) 或[参与贡献](/zh/guide/contributing)。
