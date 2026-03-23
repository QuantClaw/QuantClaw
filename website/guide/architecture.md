# Architecture Overview

QuantClaw's architecture is designed for performance, reliability, and extensibility.

## System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   User Interfaces                        │
│  CLI │ Web Dashboard │ Chat Channels (Discord, Slack)   │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│              Gateway (RPC Server)                        │
│  • HTTP/WebSocket API                                   │
│  • Command routing                                      │
│  • Session management                                   │
│  • Channel adapters (src/adapters/)                     │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│              Agent Core                                  │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ Command      │ Context      │ Memory       │         │
│  │ Queue        │ Manager      │ Manager      │         │
│  └──────────────┴──────────────┴──────────────┘         │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ Provider     │ Tool         │ Prompt       │         │
│  │ Registry     │ Registry     │ Builder      │         │
│  └──────────────┴──────────────┴──────────────┘         │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│            Plugin System (Node.js Sidecar)               │
│  • Custom skills                                        │
│  • Hooks and callbacks                                  │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│              External Integrations                       │
│  LLM Models │ Web Services │ System Calls │ Databases   │
└─────────────────────────────────────────────────────────┘
```

## Core Modules

### Agent Loop (`agent_loop.cpp`)
The main event processing engine:

- **Command Queue**: 5 operation modes (blocking, non-blocking, timeout, batching, streaming)
- **Global Concurrency**: Thread-safe request handling
- **Error Recovery**: Graceful degradation and fallback mechanisms
- **Lifecycle Management**: Initialization, running state, shutdown

```cpp
class AgentLoop {
  void Run();                    // Main event loop
  void ProcessCommand();         // Handle incoming requests
  void GenerateResponse();       // LLM interaction
  void ExecuteTools();           // Tool execution
  void ManageContext();          // Context and memory
};
```

### Context Manager (`context_manager.cpp`)
Intelligent conversation context handling:

- **Token Accounting**: Track token usage against limits
- **Context Pruning**: Remove irrelevant context via BM25 scoring
- **Overflow Handling**: Automatic compaction with retry logic
- **Message Compaction**: Summarize old messages to preserve history

### Memory Manager (`memory_manager.cpp`)
Persistent memory system:

- **Multi-tier Storage**: User, agent, and session memory
- **BM25 Search**: Efficient relevance-based retrieval
- **File Management**: Workspace file operations
- **Automatic Persistence**: Save/restore across sessions

### Prompt Builder (`prompt_builder.cpp`)
Constructs LLM prompts:

- **System Instructions**: Agent identity and capabilities
- **Context Integration**: Memory and conversation history
- **Tool Definitions**: MCP tool schema
- **Variable Substitution**: ${VAR} expansion with validation

### Tool Registry (`tool_registry.cpp`)
Manages available tools:

- **Built-in Tools**: bash, browser, process, web_search, etc.
- **MCP Integration**: Tool definitions and schemas
- **Execution Safety**: Permissions and sandboxing
- **Dynamic Loading**: Runtime plugin tool registration

### Session Manager (`session_manager.cpp`)
Manages conversation sessions:

- **Session Lifecycle**: Create, retrieve, update, delete
- **History Management**: Message persistence and retrieval
- **User Association**: Multi-user support
- **Maintenance Tasks**: Cleanup and archival

## Provider System

### LLM Provider Architecture

```cpp
class LLMProvider {
  virtual CreateMessage() = 0;        // Format request
  virtual ParseResponse() = 0;        // Handle response
  virtual GetCapabilities() = 0;      // Model info
};

class ProviderRegistry {
  RegisterProvider("anthropic", ...);
  RegisterProvider("openai", ...);
  SelectProvider(profile);             // Failover logic
};
```

### Provider Support

| Provider | Status |
|----------|--------|
| OpenAI (and compatible APIs) | ✅ Full |
| Anthropic (Claude) | ✅ Full |
| Ollama | ⚠️ Registered (stub) |
| Gemini | ⚠️ Registered (stub) |
| Mistral, Bedrock, Azure, Grok, etc. | ❌ Not yet implemented |

Any OpenAI-compatible endpoint can be used via the `providers.openai.baseUrl` setting (DeepSeek, Qwen, local models, etc.).

### Failover & Resilience
- Health monitoring per provider
- Exponential backoff on errors
- Profile rotation
- Fallback chain execution

## Tool Execution

### Execution Pipeline
1. **Parsing**: Extract tool calls from LLM response
2. **Validation**: Check schema and permissions
3. **Execution**: Run tool with sandboxing
4. **Collection**: Gather results
5. **Truncation**: Limit result size for context
6. **Formatting**: Prepare for LLM feedback

### Built-in Tools
| Tool | Purpose | Status |
|------|---------|--------|
| bash | Command execution | ✅ Active |
| browser | Chrome CDP control | ✅ Active |
| web_search | Multi-provider search | ✅ Active |
| web_fetch | HTTP + HTML parsing | ✅ Active |
| process | Background jobs | ✅ Active |
| memory_search | BM25 retrieval | ✅ Active |
| memory_get | Direct memory access | ✅ Active |
| apply_patch | Code patch application | ✅ Active |

## Plugin System Architecture

### Sidecar Runtime
Node.js subprocess for:
- Custom skills
- Hook execution
- Event handlers

### Channel Adapters
Located in `src/adapters/`:
- Discord adapter (discord.js)
- Telegram adapter (telegraf)
- Custom adapters (user-provided)

### IPC Communication
- **TCP Protocol**: `127.0.0.1:QUANTCLAW_PORT`
- **JSON-RPC**: Request/response messaging
- **Streaming**: Async event delivery
- **Error Handling**: Graceful sidecar recovery

### Plugin Manifest
```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "skills": ["skill1", "skill2"],
  "hooks": {
    "before_agent_init": "onInit",
    "on_tool_execute": "onToolExecute"
  }
}
```

## MCP (Model Context Protocol) Integration

### Resource Management
- URI-based resource identification
- MIME type support
- Resource readers
- Prompts and templates

### Handler Implementation
```cpp
void HandleListResources(const nlohmann::json& req);
void HandleReadResource(const nlohmann::json& req);
void HandleListPrompts(const nlohmann::json& req);
void HandleGetPrompt(const nlohmann::json& req);
```

## Gateway (RPC Server)

### API Endpoints
- `agent.{action}`: Agent operations
- `chat.{action}`: Conversation management
- `sessions.{action}`: Session operations
- `tools.{action}`: Tool discovery and execution
- `config.{action}`: Configuration management
- `skills.{action}`: Skill operations

### Request/Response Format
```json
{
  "jsonrpc": "2.0",
  "id": "req-123",
  "method": "agent.run",
  "params": {
    "message": "What is 2+2?",
    "session_id": "sess-abc"
  }
}
```

## Data Structures

### Context Snapshot
```cpp
struct ContextSnapshot {
  std::string session_key;
  int token_count;
  int max_tokens;
  std::vector<Message> messages;
  std::vector<UsageRecord> usage;
  int thinking_level;
};
```

### Session Record
```cpp
struct SessionRecord {
  std::string session_id;
  std::string user_id;
  int64_t created_at;
  std::vector<Message> messages;
  std::map<std::string, std::string> metadata;
};
```

### Tool Result
```cpp
struct ToolResult {
  std::string tool_name;
  bool success;
  std::string output;
  int64_t execution_time_ms;
};
```

## Security Architecture

### RBAC Implementation
- Resource-based access control
- Scope-based permissions
- Multi-tier authorization levels
- Audit logging

### Tool Sandboxing
- Process isolation
- File system restrictions
- Network policies
- Resource limits

### Configuration Validation
- Schema enforcement
- Type checking
- Permission validation
- Secure defaults

## Performance Optimizations

### Memory Efficiency
- Context compaction (summarization)
- Token budget management
- Intelligent pruning
- Overflow detection and handling

### Execution Efficiency
- Concurrent command processing
- Streaming response handling
- Tool result truncation
- Request batching

### Caching
- Configuration caching
- Tool schema caching
- Provider health caching
- Memory search indexing

## Deployment Patterns

### Single Agent (Foreground)
```bash
quantclaw onboard --quick      # First-time setup
quantclaw gateway              # Run gateway in foreground
quantclaw agent "Hello!"       # Send a message
```

### Gateway Daemon
```bash
quantclaw gateway install      # Install as system service
quantclaw gateway start        # Start the daemon
quantclaw agent "Hello!"       # Connect via gateway
```

### Containerized (Docker)
```bash
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -e OPENAI_API_KEY=sk-... \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest
```

### Production (Docker Compose)
```bash
docker compose -f scripts/docker-compose.yml up -d quantclaw
```

---

**Next**: [Learn how to develop plugins](/guide/plugins) or see the [CLI reference](/guide/cli-reference).
