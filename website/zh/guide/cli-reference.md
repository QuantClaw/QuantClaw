# CLI 参考

QuantClaw 完整命令参考。

## 全局选项

```bash
quantclaw [OPTIONS] COMMAND [ARGS]
```

- `--help, -h` — 显示帮助
- `--version, -v` — 显示版本
- `--config PATH` — 指定配置文件路径
- `--log-level LEVEL` — 日志级别：`trace`、`debug`、`info`、`warn`、`error`

## 命令

### agent

向 Agent 发送消息。

```bash
quantclaw agent [OPTIONS] MESSAGE
```

**选项：**
- `--session SESSION` — 指定会话 key（默认自动生成）

**示例：**
```bash
# 发送消息（自动创建会话）
quantclaw agent "你好，介绍一下你自己"

# 使用指定会话
quantclaw agent --session my:project "项目进展如何？"
```

### run

向 Agent 发送消息（`agent` 的别名）。

```bash
quantclaw run MESSAGE
```

### eval

一次性 Prompt 评估——不创建也不使用会话历史。

```bash
quantclaw eval PROMPT
```

**示例：**
```bash
quantclaw eval "2 + 2 等于多少？"
quantclaw eval "生成一个随机 UUID"
```

### gateway

管理 RPC 网关。

```bash
quantclaw gateway [SUBCOMMAND] [OPTIONS]
```

#### gateway（无子命令）
在前台运行网关。

```bash
quantclaw gateway
```

#### gateway install
安装为系统服务（Linux: systemd，macOS: launchd）。

```bash
quantclaw gateway install
```

#### gateway uninstall
卸载系统服务。

```bash
quantclaw gateway uninstall
```

#### gateway start / stop / restart
控制后台 Daemon。

```bash
quantclaw gateway start
quantclaw gateway stop
quantclaw gateway restart
```

#### gateway status
查看 Daemon 是否正在运行。

```bash
quantclaw gateway status
```

#### gateway call
直接调用任意 RPC 方法。

```bash
quantclaw gateway call METHOD [JSON_PARAMS]
```

**示例：**
```bash
quantclaw gateway call gateway.health
quantclaw gateway call config.get '{"path":"llm.model"}'
```

### sessions

管理对话会话。

```bash
quantclaw sessions SUBCOMMAND
```

```bash
quantclaw sessions list
quantclaw sessions history SESSION_KEY
quantclaw sessions delete SESSION_KEY
quantclaw sessions reset SESSION_KEY
```

### config

管理配置。

```bash
quantclaw config SUBCOMMAND
```

```bash
quantclaw config get                    # 查看完整配置
quantclaw config get llm.model         # 查看指定配置项（点路径）
quantclaw config set llm.model "anthropic/claude-sonnet-4-6"
quantclaw config unset llm.temperature
quantclaw config reload                # 热重载（无需重启网关）
quantclaw config validate              # 验证语法
quantclaw config schema                # 查看 Schema
```

### skills

管理技能。

```bash
quantclaw skills list              # 列出已加载技能
quantclaw skills install NAME      # 安装技能依赖
```

### memory

搜索和查看 Agent 记忆。

```bash
quantclaw memory search "查询内容"
quantclaw memory search "近期事件" --limit 10
quantclaw memory status
```

**选项：**
- `--limit N` — 结果数量限制（默认 5）

### cron

管理定时任务。

```bash
quantclaw cron list                            # 列出所有定时任务
quantclaw cron add NAME "0 9 * * *" "TASK"    # 添加任务（cron 表达式）
quantclaw cron remove TASK_ID                 # 按 ID 删除任务
```

### health

快速健康检查——确认网关是否可达。

```bash
quantclaw health
```

### status

显示连接数和会话数。

```bash
quantclaw status
```

### logs

实时查看网关日志。

```bash
quantclaw logs
```

### doctor

运行完整诊断检查。

```bash
quantclaw doctor
```

### dashboard

在浏览器中打开 Web 仪表板。

```bash
quantclaw dashboard
```

打开 `http://127.0.0.1:18801`。

### onboard

交互式设置向导。

```bash
quantclaw onboard [OPTIONS]
```

**选项：**
- `--quick` — 快速设置（无交互）
- `--install-daemon` — 同时安装网关为系统服务

**示例：**
```bash
quantclaw onboard                     # 交互式
quantclaw onboard --quick             # 无交互
quantclaw onboard --install-daemon    # 交互式 + 安装 Daemon
```

## 对话内消息指令

与 AI 对话时，发送以斜杠开头的消息即可控制当前会话：

| 指令 | 效果 |
|------|------|
| `/new` | 开启新会话 |
| `/reset` | 清空当前会话历史 |
| `/compact` | 手动触发上下文压缩 |
| `/status` | 显示当前会话和队列状态 |
| `/commands` | 列出所有可用斜杠指令 |
| `/help` | 显示帮助信息 |

## 环境变量

| 变量 | 说明 |
|------|------|
| `OPENAI_API_KEY` | OpenAI / 兼容 Provider API Key |
| `ANTHROPIC_API_KEY` | Anthropic API Key |
| `QUANTCLAW_LOG_LEVEL` | 日志级别覆盖（`debug`、`info`、`warn`、`error`）|
| `QUANTCLAW_PORT` | Sidecar IPC 端口（内部使用，自动设置）|

## 端口

| 服务 | 端口 | 用途 |
|------|------|------|
| WebSocket RPC 网关 | `18800` | 主网关，客户端连接入口 |
| HTTP REST API / 仪表板 | `18801` | Web UI 和 REST API |

## 完整工作流示例

```bash
# 1. 初始化
quantclaw onboard --quick

# 2. 后台启动网关
quantclaw gateway start

# 3. 发送消息
quantclaw agent "你好！"

# 4. 查看会话历史
quantclaw sessions list
quantclaw sessions history SESSION_KEY

# 5. 搜索记忆
quantclaw memory search "项目笔记"

# 6. 检查状态
quantclaw health
quantclaw status

# 7. 查看日志
quantclaw logs
```

---

**需要帮助？** 运行 `quantclaw --help` 或 `quantclaw COMMAND --help`。
