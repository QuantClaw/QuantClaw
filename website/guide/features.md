# Core Features

QuantClaw combines powerful AI capabilities with local-first execution and robust error handling.

## 🧠 Intelligent Conversation

Multi-turn dialogue with full context awareness:

- **Context Management**: Automatic compaction and pruning to stay within token limits
- **Thinking Mode**: Enable extended reasoning for complex problems
- **Session History**: Persistent conversation tracking and replay
- **Memory Integration**: Automatic context retrieval from knowledge bases

```bash
quantclaw run "Help me analyze this code"
```

## 💾 Persistent Memory System

Advanced memory management inspired by human cognition:

### Memory Types
- **User Memory**: Persistent user profiles and preferences
- **Agent Memory**: Long-term learned patterns and behaviors
- **Working Memory**: Current session context
- **Workspace Files**: SOUL, IDENTITY, HEARTBEAT, MEMORY, SKILL

### Memory Operations
```bash
quantclaw memory search "user preferences"
quantclaw memory get workspace/MEMORY.md
```

### Automatic Context Pruning
- BM25-based relevance scoring
- Overflow compaction with 3-attempt retry
- Budget-based context management
- Thinking level auto-adjustment

## 🌐 Browser Control

Full Chrome DevTools Protocol integration:

Browser tools (`browser.launch`, `browser.execute`, `browser.screenshot`) are built-in agent tools invoked automatically during a session. They are not direct CLI commands.

### Capabilities
- **Page Navigation**: Load and control URLs
- **DOM Interaction**: Click, type, submit forms
- **JavaScript Execution**: Run custom scripts
- **Screenshot Capture**: Visual verification
- **Cookie Management**: Session persistence
- **Network Monitoring**: Intercept requests

## 💻 System Integration

Execute commands and manage system resources:

### Command Execution
The `bash` and `process` tools are built-in agent tools (not direct CLI commands). The agent uses them automatically when executing tasks.

### File Operations
```bash
# Workspace files are accessed via the agent or directly from ~/.quantclaw/agents/main/workspace/
ls ~/.quantclaw/agents/main/workspace/
cat ~/.quantclaw/agents/main/workspace/MEMORY.md
```

### Environment Access
```bash
quantclaw config get llm.model
```

## 🔌 Plugin Ecosystem

Extensible architecture with Node.js sidecar:

### Built-in Plugins
- **Bash Tools**: Safe command execution
- **Browser Control**: CDP integration
- **Web Search**: Multi-provider search (Tavily, DuckDuckGo, Perplexity)
- **Web Fetch**: HTML scraping with SSRF protection
- **Memory Search**: BM25-based knowledge retrieval
- **Cron Scheduling**: Automated task execution
- **Process Management**: Background job control

### Custom Plugin Development
```bash
quantclaw skills list
quantclaw skills install ./my-plugin
```

## 🔄 Multi-Provider LLM Support

OpenAI-compatible and Anthropic APIs with automatic failover:

### Supported Providers
- **OpenAI** (and compatible endpoints): qwen-max, GPT-4, DeepSeek, local Ollama, etc.
- **Anthropic**: Claude Haiku, Sonnet, Opus
- **Ollama / Gemini**: Registered (stub, not fully implemented)

Any OpenAI-compatible API endpoint can be used by setting `providers.openai.baseUrl`.

### Provider Selection
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

### Automatic Failover
- Exponential backoff on failures
- Profile rotation and health monitoring
- Fallback chain execution
- Token usage accumulation

## 🛡️ Enterprise Security

Production-grade security controls:

### Role-Based Access Control (RBAC)
RBAC is enforced at the gateway level. Roles include:
- `agent.admin`, `operator.read`, `operator.write`, `viewer`

### Tool Permissions
- Per-tool allow/deny rules
- Command pattern matching
- Execution approvals
- Audit logging

### Sandboxing
- Process isolation
- File system restrictions
- Network policies
- Resource limits

### Multi-level Authorization
- Agent-level: `agent.admin`, `agent.operator`, `agent.viewer`
- Tool-level: `tool.{name}.read`, `tool.{name}.write`
- Session-level: User-scoped operations

## 📊 Usage Tracking

Comprehensive monitoring and analytics:

```bash
quantclaw usage cost          # Total token costs
quantclaw sessions usage      # Session-by-session breakdown
quantclaw sessions list              # List sessions
quantclaw logs tail          # Real-time logs
```

### Metrics Collected
- Token usage by provider and model
- Request latency
- Error rates
- Provider health
- Session duration

## 📱 Multi-Channel Integration

Connect across communication platforms:

### Supported Channels
- **Discord**: External subprocess adapter
- **Telegram**: External subprocess adapter
- **Web dashboard**: Built-in at port 18801

### Channel Configuration
```json
{
  "channels": {
    "discord": {
      "token": "YOUR_BOT_TOKEN"
    },
    "telegram": {
      "token": "YOUR_BOT_TOKEN"
    }
  }
}
```

## 🎯 Workflow Automation

Scheduled and triggered automation:

### Cron Jobs
```bash
quantclaw cron add "daily-report" "0 9 * * *" "Send daily summary"
quantclaw cron list
quantclaw cron remove TASK_ID
```

### Hooks and Callbacks
- Pre/post-processing hooks
- Custom event handlers
- Lifecycle callbacks
- Error recovery handlers

## 📊 Performance Optimizations

- **C++17 Native**: Direct compilation for target platform
- **Efficient Memory**: Context compaction and pruning
- **Fast Startup**: Lazy initialization of modules
- **Concurrent Operations**: Multi-threaded request handling
- **Smart Batching**: Grouped API calls

---

**Next**: [Learn about the architecture](/guide/architecture) or [start building plugins](/guide/plugins).
