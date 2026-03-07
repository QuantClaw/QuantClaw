---
layout: home

hero:
  name: "QuantClaw"
  text: "High-Performance AI Agent Framework"
  tagline: "C++17 native implementation of OpenClaw with persistent memory, browser control, and plugin ecosystem"
  image:
    src: /logo.png
    alt: QuantClaw
  actions:
    - theme: brand
      text: Get Started
      link: /guide/getting-started
    - theme: alt
      text: View on GitHub
      link: https://github.com/QuantClaw/QuantClaw

features:
  - icon: ⚡
    title: "High Performance"
    details: "C++17 native binary with minimal overhead — no interpreter, no GC pauses, tiny memory footprint"

  - icon: 🧠
    title: "Context Management"
    details: "Automatic compaction, BM25 memory search, budget-based pruning, and overflow compaction retry"

  - icon: 🌐
    title: "Browser Control"
    details: "Real Chrome DevTools Protocol over WebSocket — navigation, JS execution, screenshots, and more"

  - icon: 🔌
    title: "Plugin Ecosystem"
    details: "Full OpenClaw plugin compatibility via Node.js sidecar — tools, hooks, services, HTTP routes, gateway methods"

  - icon: 🛡️
    title: "Sandboxed Execution"
    details: "RBAC authorization, setrlimit sandbox, exec approval, path allowlist/denylist, and rate limiting"

  - icon: 📱
    title: "Multi-Platform"
    details: "Runs natively on Ubuntu and Windows, with Docker Compose support for production deployments"

  - icon: 🔄
    title: "Multi-Provider LLM"
    details: "OpenAI and Anthropic fully supported; provider/model prefix routing; exponential backoff failover"

  - icon: 💬
    title: "Channel Adapters"
    details: "Connect Discord and Telegram bots via external subprocess adapters using the WebSocket RPC protocol"

  - icon: 🎯
    title: "OpenClaw Compatible"
    details: "Works with OpenClaw workspace files, skill format, JSONL sessions, and the WebSocket RPC protocol"
---

## Why QuantClaw?

**QuantClaw** is a C++17 reimplementation of the [OpenClaw](https://github.com/openclaw/openclaw) AI agent ecosystem — built for performance and low memory footprint while staying compatible with OpenClaw workspace files, skills, and the RPC protocol.

- **Native Performance**: C++17 binary, no Node.js runtime overhead
- **OpenClaw Compatible**: Workspace files, SKILL.md format, JSONL sessions, and WebSocket RPC
- **Plugin Ecosystem**: Run OpenClaw TypeScript plugins via Node.js sidecar (TCP loopback IPC)
- **Production Ready**: Daemon mode, Docker support, RBAC, sandbox, failover

## Quick Comparison

| Feature | QuantClaw | OpenClaw |
|---------|-----------|----------|
| Language | C++17 | TypeScript/Node.js |
| Runtime Overhead | Minimal | Node.js VM |
| Memory Footprint | Low | Medium |
| Plugin Support | ✅ Sidecar (TCP IPC) | ✅ Native (in-process) |
| CLI Compatibility | Core commands | Full |
| Workspace Files | ✅ All 8 | ✅ All 8 |
| Config Format | JSON5 + `${VAR}` | JSON5 + `${VAR}` + `$include` |

## Key Capabilities

### Intelligent Conversation
- Multi-turn dialogue with automatic context management
- Dynamic iterations (32–160) based on task complexity
- Context compaction with 3-attempt overflow retry
- Session persistence in JSONL format

### Persistent Memory
- 8 workspace files: SOUL.md, MEMORY.md, SKILL.md, IDENTITY.md, HEARTBEAT.md, USER.md, AGENTS.md, TOOLS.md
- BM25 memory search across all workspace files
- Budget-based context pruning

### Browser Automation
- Real Chrome DevTools Protocol (WebSocket)
- Page navigation, DOM interaction, JS execution, screenshots
- Graceful fallback when no browser is available

### System Integration
- `bash` tool with sandboxed execution
- `apply_patch` for code patching (*** Begin Patch format)
- `process` for background job management
- `web_search` (Tavily → DuckDuckGo cascade) and `web_fetch`

### Plugin Ecosystem
- 24 lifecycle hook types (void/modifying/sync)
- Custom tools, services, providers, commands, HTTP routes, gateway methods
- TCP loopback IPC — works identically on Linux and Windows

## Getting Started

```bash
# Clone and build
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
mkdir build && cd build
cmake .. && make -j$(nproc)

# Initialize
quantclaw onboard --quick

# Start gateway and chat
quantclaw gateway start
quantclaw agent "Hello, introduce yourself"
```

For detailed instructions, see [Getting Started](/guide/getting-started).

## Community & Support

- **GitHub**: [QuantClaw/QuantClaw](https://github.com/QuantClaw/QuantClaw)
- **Issues**: [Report bugs and request features](https://github.com/QuantClaw/QuantClaw/issues)
- **Discussions**: [Community discussions](https://github.com/QuantClaw/QuantClaw/discussions)

## License

QuantClaw is released under the [Apache 2.0 License](https://github.com/QuantClaw/QuantClaw/blob/main/LICENSE).

---

**Built with ❤️ in C++17. Inspired by [OpenClaw](https://github.com/openclaw/openclaw).**
