---
layout: home

hero:
  name: "QuantClaw"
  text: "高性能 AI Agent 框架"
  tagline: "C++17 原生实现的 OpenClaw——持久记忆、浏览器控制、完整插件生态"
  image:
    src: /logo.png
    alt: QuantClaw
  actions:
    - theme: brand
      text: 快速开始
      link: /zh/guide/getting-started
    - theme: alt
      text: 在 GitHub 查看
      link: https://github.com/QuantClaw/QuantClaw

features:
  - icon: ⚡
    title: "原生性能"
    details: "C++17 编译为原生二进制，无解释器、无 GC 停顿，内存占用极低"

  - icon: 🧠
    title: "上下文治理"
    details: "自动压缩、BM25 记忆搜索、Budget-based 裁剪与三次 overflow 重试"

  - icon: 🌐
    title: "浏览器控制"
    details: "通过 WebSocket 连接真实 Chrome DevTools Protocol——导航、JS 执行、截图等"

  - icon: 🔌
    title: "插件生态"
    details: "通过 Node.js Sidecar 完全兼容 OpenClaw 插件——工具、钩子、服务、HTTP 路由、网关方法"

  - icon: 🛡️
    title: "沙箱执行"
    details: "RBAC 授权、setrlimit 沙箱、exec approval、路径白名单/黑名单、限流"

  - icon: 📱
    title: "跨平台"
    details: "Ubuntu 和 Windows 原生运行，同时支持 Docker Compose 生产部署"

  - icon: 🔄
    title: "多模型支持"
    details: "完整支持 OpenAI 和 Anthropic；provider/model 前缀路由；指数退避故障转移"

  - icon: 💬
    title: "频道适配器"
    details: "通过 WebSocket RPC 协议接入 Discord 和 Telegram 外部 subprocess 适配器"

  - icon: 🎯
    title: "OpenClaw 兼容"
    details: "兼容 OpenClaw 工作空间文件、SKILL.md 格式、JSONL 会话和 WebSocket RPC 协议"
---

## 为什么选择 QuantClaw？

**QuantClaw** 是 [OpenClaw](https://github.com/openclaw/openclaw) AI Agent 生态的 C++17 重新实现——专注于性能和低内存占用，同时保持与 OpenClaw 工作空间文件、技能和 RPC 协议的完全兼容。

- **原生性能**：C++17 二进制，无 Node.js 运行时开销
- **OpenClaw 兼容**：工作空间文件、SKILL.md 格式、JSONL 会话、WebSocket RPC
- **插件生态**：通过 Node.js Sidecar（TCP loopback IPC）运行 OpenClaw TypeScript 插件
- **生产就绪**：Daemon 模式、Docker 支持、RBAC、沙箱、故障转移

## 快速对比

| 特性 | QuantClaw | OpenClaw |
|------|-----------|----------|
| 语言 | C++17 | TypeScript/Node.js |
| 运行时开销 | 极低 | Node.js VM |
| 内存占用 | 低 | 中 |
| 插件支持 | ✅ Sidecar（TCP IPC）| ✅ 原生（进程内）|
| CLI 兼容性 | 核心命令 | 完整 |
| 工作空间文件 | ✅ 全部 8 个 | ✅ 全部 8 个 |
| 配置格式 | JSON5 + `${VAR}` | JSON5 + `${VAR}` + `$include` |

## 核心能力

### 智能对话
- 多轮对话，自动上下文管理
- 动态迭代次数（32–160），根据任务复杂度调整
- 三次重试的 overflow 压缩
- JSONL 格式会话持久化

### 持久记忆
- 8 个工作空间文件：SOUL.md、MEMORY.md、SKILL.md、IDENTITY.md、HEARTBEAT.md、USER.md、AGENTS.md、TOOLS.md
- 跨工作空间文件的 BM25 记忆搜索
- Budget-based 上下文裁剪

### 浏览器自动化
- 真实 Chrome DevTools Protocol（WebSocket）
- 页面导航、DOM 交互、JS 执行、截图
- 无浏览器时优雅降级

### 系统集成
- `bash` 工具（沙箱执行）
- `apply_patch`（`*** Begin Patch` 格式代码补丁）
- `process` 后台进程管理
- `web_search`（Tavily → DuckDuckGo 瀑布式）和 `web_fetch`

### 插件生态
- 24 种生命周期 Hook 类型（void/modifying/sync）
- 自定义工具、服务、Provider、命令、HTTP 路由、网关方法
- TCP loopback IPC——Linux 和 Windows 完全一致

## 快速上手

```bash
# 克隆并编译
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
mkdir build && cd build
cmake .. && make -j$(nproc)

# 初始化
quantclaw onboard --quick

# 启动网关并对话
quantclaw gateway start
quantclaw agent "你好，介绍一下你自己"
```

详细说明请参阅[快速开始指南](/zh/guide/getting-started)。

## 社区与支持

- **GitHub**：[QuantClaw/QuantClaw](https://github.com/QuantClaw/QuantClaw)
- **Issues**：[报告 Bug 或提新功能](https://github.com/QuantClaw/QuantClaw/issues)
- **Discussions**：[社区讨论](https://github.com/QuantClaw/QuantClaw/discussions)

## 许可证

QuantClaw 基于 [Apache 2.0 协议](https://github.com/QuantClaw/QuantClaw/blob/main/LICENSE)发布。

---

**用 C++17 构建，灵感来自 [OpenClaw](https://github.com/openclaw/openclaw)。**
