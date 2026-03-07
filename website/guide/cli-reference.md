# CLI Reference

Complete command reference for QuantClaw.

## Global Options

```bash
quantclaw [OPTIONS] COMMAND [ARGS]
```

- `--help, -h` — Show help message
- `--version, -v` — Show version
- `--config PATH` — Configuration file path
- `--log-level LEVEL` — Log level: `trace`, `debug`, `info`, `warn`, `error`

## Commands

### agent

Send a message to the agent.

```bash
quantclaw agent [OPTIONS] MESSAGE
```

**Options:**
- `--session SESSION` — Session key to use (default: auto-generated)

**Examples:**
```bash
# Send a message (creates a new session automatically)
quantclaw agent "Hello, introduce yourself"

# Use a specific session key
quantclaw agent --session my:project "What's the status?"
```

### run

Send a message to the agent (alias for `agent`).

```bash
quantclaw run MESSAGE
```

### eval

One-shot prompt evaluation — no session history is created or used.

```bash
quantclaw eval PROMPT
```

**Examples:**
```bash
quantclaw eval "What is 2 + 2?"
quantclaw eval "Generate a random UUID"
```

### gateway

Manage the RPC gateway.

```bash
quantclaw gateway [SUBCOMMAND] [OPTIONS]
```

**Subcommands:**

#### gateway (no subcommand)
Run the gateway in the foreground.

```bash
quantclaw gateway
```

#### gateway install
Install as a system service (systemd on Linux, launchd on macOS).

```bash
quantclaw gateway install
```

#### gateway uninstall
Remove the system service.

```bash
quantclaw gateway uninstall
```

#### gateway start / stop / restart
Control the background daemon.

```bash
quantclaw gateway start
quantclaw gateway stop
quantclaw gateway restart
```

#### gateway status
Check whether the gateway daemon is running.

```bash
quantclaw gateway status
```

#### gateway call
Call any RPC method directly.

```bash
quantclaw gateway call METHOD [JSON_PARAMS]
```

**Examples:**
```bash
quantclaw gateway call gateway.health
quantclaw gateway call config.get '{"path":"llm.model"}'
```

### sessions

Manage conversation sessions.

```bash
quantclaw sessions SUBCOMMAND [OPTIONS]
```

#### sessions list

```bash
quantclaw sessions list
```

#### sessions history

```bash
quantclaw sessions history SESSION_KEY
```

#### sessions delete

```bash
quantclaw sessions delete SESSION_KEY
```

#### sessions reset

```bash
quantclaw sessions reset SESSION_KEY
```

### config

Manage configuration.

```bash
quantclaw config SUBCOMMAND [OPTIONS]
```

#### config get

```bash
quantclaw config get                    # Full config
quantclaw config get llm.model         # Specific value (dot-path)
```

#### config set

```bash
quantclaw config set llm.model "anthropic/claude-sonnet-4-6"
```

#### config unset

```bash
quantclaw config unset llm.temperature
```

#### config reload
Hot-reload config without restarting the gateway.

```bash
quantclaw config reload
```

#### config validate

```bash
quantclaw config validate
```

#### config schema

```bash
quantclaw config schema
```

### skills

Manage skills.

```bash
quantclaw skills SUBCOMMAND
```

#### skills list

```bash
quantclaw skills list
```

#### skills install

Install a skill's dependencies.

```bash
quantclaw skills install SKILL_NAME
```

### memory

Search and inspect agent memory.

```bash
quantclaw memory SUBCOMMAND [OPTIONS]
```

#### memory search

```bash
quantclaw memory search "query string"
quantclaw memory search "recent events" --limit 10
```

**Options:**
- `--limit N` — Result limit (default: 5)

#### memory status

```bash
quantclaw memory status
```

### cron

Manage scheduled tasks.

```bash
quantclaw cron SUBCOMMAND
```

#### cron list

```bash
quantclaw cron list
```

#### cron add

```bash
quantclaw cron add NAME "0 9 * * *" "Send daily summary"
```

#### cron remove

```bash
quantclaw cron remove TASK_ID
```

### health

Quick health check — confirms the gateway is reachable.

```bash
quantclaw health
```

### status

Show connection and session counts.

```bash
quantclaw status
```

### logs

Stream gateway logs.

```bash
quantclaw logs
```

### doctor

Run a full diagnostic check.

```bash
quantclaw doctor
```

### dashboard

Open the web dashboard in the browser.

```bash
quantclaw dashboard
```

Opens `http://127.0.0.1:18801`.

### onboard

Interactive setup wizard.

```bash
quantclaw onboard [OPTIONS]
```

**Options:**
- `--quick` — Quick setup with defaults (non-interactive)
- `--install-daemon` — Also install the gateway as a system service

**Examples:**
```bash
quantclaw onboard                     # Interactive
quantclaw onboard --quick             # Non-interactive
quantclaw onboard --install-daemon    # Interactive + install daemon
```

## In-Conversation Message Commands

While chatting, prefix a message with a slash command to control the session:

| Command | Effect |
|---------|--------|
| `/new` | Start a new session |
| `/reset` | Clear current session history |
| `/compact` | Manually trigger context compaction |
| `/status` | Show current session and queue status |
| `/commands` | List all available slash commands |
| `/help` | Show help text |

## Environment Variables

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI / compatible provider API key |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `QUANTCLAW_LOG_LEVEL` | Log level override (`debug`, `info`, `warn`, `error`) |
| `QUANTCLAW_PORT` | Sidecar IPC port (internal use, set automatically) |

## Ports

| Service | Port | Purpose |
|---------|------|---------|
| WebSocket RPC Gateway | `18800` | Main gateway for client connections |
| HTTP REST API / Dashboard | `18801` | Web UI and REST API |

## Complete Workflow Example

```bash
# 1. Initial setup
quantclaw onboard --quick

# 2. Start gateway in background
quantclaw gateway start

# 3. Send a message
quantclaw agent "Hello!"

# 4. View session history
quantclaw sessions list
quantclaw sessions history SESSION_KEY

# 5. Search memory
quantclaw memory search "project notes"

# 6. Check status
quantclaw health
quantclaw status

# 7. Stream logs
quantclaw logs
```

---

**Need help?** Run `quantclaw --help` or `quantclaw COMMAND --help`.
