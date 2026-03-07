# 文档概览

QuantClaw 完整文档导航。

## 入门

| 文档 | 说明 |
|------|------|
| [快速开始](/zh/guide/getting-started) | 5 分钟内完成安装并运行第一次对话 |
| [安装说明](/zh/guide/installation) | Linux、Windows、macOS 及 Docker 详细安装指南 |
| [配置参考](/zh/guide/configuration) | 完整配置选项说明（含示例） |

## 核心功能

| 文档 | 说明 |
|------|------|
| [核心特性](/zh/guide/features) | 智能对话、记忆、浏览器控制、安全等所有特性 |
| [架构说明](/zh/guide/architecture) | 系统架构、核心模块、IPC 协议 |
| [CLI 参考](/zh/guide/cli-reference) | 所有命令、选项和使用示例 |

## 开发

| 文档 | 说明 |
|------|------|
| [插件开发](/zh/guide/plugins) | 创建自定义工具、Hook 和技能 |
| [从源码构建](/zh/guide/building) | 编译选项、Docker 构建、测试运行 |
| [参与贡献](/zh/guide/contributing) | 代码规范、工作流程、PR 指南 |

## 外部资源

- **GitHub 仓库**：[QuantClaw/QuantClaw](https://github.com/QuantClaw/QuantClaw)
- **Issues**：[报告 Bug](https://github.com/QuantClaw/QuantClaw/issues)
- **Discussions**：[社区讨论](https://github.com/QuantClaw/QuantClaw/discussions)
- **OpenClaw 参考**：[openclaw/openclaw](https://github.com/openclaw/openclaw)

## 关键路径快速参考

### 文件路径

```
~/.quantclaw/
├── quantclaw.json              # 配置文件
├── skills/                     # 全局技能目录
└── agents/main/
    ├── workspace/
    │   ├── SOUL.md             # 助手身份
    │   ├── MEMORY.md           # 长期记忆
    │   ├── SKILL.md            # 技能声明
    │   ├── IDENTITY.md         # 能力自述
    │   ├── HEARTBEAT.md        # Cron 日志
    │   ├── USER.md             # 用户偏好
    │   ├── AGENTS.md           # Agent 列表
    │   └── TOOLS.md            # 工具指引
    └── sessions/
        ├── sessions.json       # 会话索引
        └── <session-id>.jsonl  # 单会话记录
```

### 端口

| 服务 | 端口 |
|------|------|
| WebSocket RPC 网关 | `18800` |
| HTTP REST / Web 仪表板 | `18801` |

### 常用命令速查

```bash
quantclaw onboard --quick       # 初始化
quantclaw gateway start         # 后台启动网关
quantclaw agent "你好！"        # 发送消息
quantclaw eval "2+2"           # 一次性查询
quantclaw sessions list        # 查看会话
quantclaw memory search "..."  # 搜索记忆
quantclaw health               # 健康检查
quantclaw config get           # 查看配置
quantclaw doctor               # 诊断
quantclaw dashboard            # 打开 Web UI
```
