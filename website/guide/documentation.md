# Full Documentation

Complete reference for QuantClaw features and APIs.

## Table of Contents

- [Quick Start](#quick-start)
- [Installation](#installation)
- [Configuration](#configuration)
- [CLI Reference](#cli-reference)
- [API Reference](#api-reference)
- [Plugin Development](#plugin-development)
- [Architecture](#architecture)
- [Troubleshooting](#troubleshooting)

## Quick Start

Get QuantClaw running in 5 minutes:

```bash
# 1. Install
git clone https://github.com/QuantClaw/quantclaw.git
cd quantclaw
mkdir build && cd build
cmake ..
cmake --build . --parallel

# 2. Configure
quantclaw onboard --quick

# 3. Run
quantclaw agent --id=main
```

Then open the web interface at `http://localhost:8000`.

## Installation

See the [Installation Guide](/guide/installation) for:
- Linux/Ubuntu setup
- Windows (WSL2) setup
- macOS setup
- Docker setup
- Building from source

## Configuration

See the [Configuration Guide](/guide/configuration) for:
- Configuration file structure
- Provider setup
- Model selection
- Memory configuration
- Tool permissions
- Channel setup

## CLI Reference

See the [CLI Reference](/guide/cli-reference) for complete command documentation.

### Common Commands

```bash
# Agent operations
quantclaw agent --id=main              # Start agent
quantclaw agent --id=main --port 9000  # Custom port

# Run commands
quantclaw run "What is 2+2?"          # Execute command
quantclaw eval "What is 2+2?" --no-session  # Eval without session

# Session management
quantclaw sessions list                # List sessions
quantclaw sessions history SESS-ID     # View session history
quantclaw sessions compact SESS-ID     # Compact session

# Configuration
quantclaw config get                   # View config
quantclaw config validate              # Validate config
quantclaw config schema                # Show schema

# Gateway
quantclaw gateway run                  # Start gateway
quantclaw gateway status               # Check gateway status

# File operations
quantclaw file list                    # List workspace files
quantclaw file read WORKSPACE/FILE     # Read file
quantclaw file write WORKSPACE/FILE    # Write file

# Skills
quantclaw skill create my-skill        # Create new skill
quantclaw skill install ./my-skill     # Install plugin
quantclaw skill status                 # List installed skills

# Status and monitoring
quantclaw status                       # Overall status
quantclaw logs tail                    # View logs
quantclaw usage cost                   # Token usage
```

## API Reference

QuantClaw exposes a JSON-RPC API via the gateway.

### Gateway Endpoints

**Base URL**: `http://localhost:8000`

#### Agent Operations

```
POST /api/rpc
Content-Type: application/json

{
  "jsonrpc": "2.0",
  "id": "req-1",
  "method": "agent.run",
  "params": {
    "message": "What is 2+2?",
    "session_id": "optional-session-id"
  }
}
```

#### Session Management

```json
{
  "method": "sessions.list",
  "params": {}
}

{
  "method": "sessions.history",
  "params": {
    "session_id": "SESS-123"
  }
}

{
  "method": "sessions.send",
  "params": {
    "session_id": "SESS-123",
    "message": "Continue previous conversation"
  }
}
```

#### Configuration

```json
{
  "method": "config.get",
  "params": {}
}

{
  "method": "config.validate",
  "params": {}
}

{
  "method": "config.schema",
  "params": {}
}
```

#### Tools

```json
{
  "method": "tools.catalog",
  "params": {
    "agent_id": "main"
  }
}

{
  "method": "tools.execute",
  "params": {
    "name": "bash",
    "params": {
      "command": "ls -la"
    }
  }
}
```

#### Monitoring

```json
{
  "method": "usage.cost",
  "params": {}
}

{
  "method": "sessions.usage",
  "params": {}
}

{
  "method": "logs.tail",
  "params": {
    "lines": 100
  }
}
```

### WebSocket Connection

For real-time updates, connect via WebSocket:

```javascript
const ws = new WebSocket('ws://localhost:8000/ws');

ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  console.log('Received:', message);
};

// Send RPC request
ws.send(JSON.stringify({
  jsonrpc: '2.0',
  id: 'req-1',
  method: 'agent.run',
  params: { message: 'Hello' }
}));
```

## Plugin Development

See the [Plugin Development Guide](/guide/plugins) for:
- Creating custom skills
- Implementing hooks
- Building channel adapters
- Testing plugins
- Publishing plugins

### Quick Example

```typescript
import type { QuantClawPlugin, SkillContext } from '@quantclaw/sdk'

export default class MyPlugin implements QuantClawPlugin {
  async initialize(context: SkillContext) {
    console.log('Plugin initialized')
  }

  getSkills() {
    return [{
      name: 'my_skill',
      description: 'My custom skill',
      input_schema: {
        type: 'object',
        properties: {
          input: { type: 'string' }
        }
      }
    }]
  }

  async executeSkill(name: string, params: any) {
    if (name === 'my_skill') {
      return JSON.stringify({ result: params.input })
    }
    throw new Error(`Unknown skill: ${name}`)
  }
}
```

## Architecture

See the [Architecture Guide](/guide/architecture) for:
- System overview
- Core modules
- Data flow
- Security model
- Performance optimizations

## Built-in Tools

### Bash Execution

```bash
quantclaw tool bash "ls -la /home"
```

**Schema**:
```json
{
  "name": "bash",
  "description": "Execute bash commands",
  "input_schema": {
    "properties": {
      "command": { "type": "string" }
    },
    "required": ["command"]
  }
}
```

### Browser Control

```bash
quantclaw tool browser.launch "https://example.com"
```

**Schema**:
```json
{
  "name": "browser",
  "description": "Chrome DevTools Protocol control",
  "input_schema": {
    "properties": {
      "action": {
        "type": "string",
        "enum": ["launch", "navigate", "screenshot", "execute"]
      }
    }
  }
}
```

### Web Search

```bash
quantclaw tool web_search "latest AI news"
```

Supports multiple providers:
- Tavily (professional API)
- DuckDuckGo (free)
- Perplexity
- Grok

### Web Fetch

```bash
quantclaw tool web_fetch "https://example.com"
```

Features:
- HTML to markdown conversion
- SSRF protection
- Timeout handling
- Cookie support

### Memory Search

```bash
quantclaw tool memory_search "user preferences"
```

Uses BM25 scoring for relevance.

### Memory Get

```bash
quantclaw tool memory_get "WORKSPACE/MEMORY.md"
```

Direct file access from workspace.

## Troubleshooting

### Common Issues

**Build errors**
```bash
# Clean and rebuild
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
```

**Configuration errors**
```bash
# Validate configuration
quantclaw config validate

# Reset to defaults
quantclaw onboard --reset
```

**Plugin not loading**
```bash
# Check plugin directory
ls ~/.quantclaw/plugins/

# View logs
quantclaw logs tail --level=debug
```

**API connection issues**
```bash
# Verify gateway is running
quantclaw gateway status

# Check port
netstat -tuln | grep 8000

# Test connection
curl http://localhost:8000/health
```

## Performance Tips

1. **Model Selection**: Use `fast` model for quick tasks
2. **Context Management**: Enable context compaction
3. **Memory**: Limit search results with `searchTopK`
4. **Tool Timeout**: Set reasonable timeouts
5. **Logging**: Use `warn` level in production

## Security Best Practices

1. **API Keys**: Store in environment variables
2. **HTTPS**: Enable TLS in gateway config
3. **Tool Permissions**: Restrict tool access
4. **Sandboxing**: Enable process isolation
5. **Audit Logs**: Enable and monitor logs

## Advanced Topics

### Custom Models

Configure Ollama or local models:

```json
{
  "agents": {
    "main": {
      "providers": {
        "default": "local"
      },
      "models": {
        "default": "llama2"
      }
    }
  }
}
```

### Multi-Agent Setup

Run multiple agents on the same gateway:

```bash
quantclaw gateway run &
quantclaw agent --id=main &
quantclaw agent --id=reasoning &
```

### Distributed Deployment

Deploy across multiple machines with shared gateway:

```bash
# Machine 1: Run gateway
quantclaw gateway run --host 0.0.0.0

# Machine 2: Connect to remote gateway
quantclaw agent --id=main --gateway machine1:8000
```

---

**Need help?** Check the [Getting Started](/guide/getting-started) guide or open an issue on [GitHub](https://github.com/QuantClaw/quantclaw/issues).
