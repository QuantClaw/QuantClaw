# Getting Started with QuantClaw

Welcome to QuantClaw! This guide will help you get up and running in just a few minutes.

## What is QuantClaw?

QuantClaw is a high-performance C++17 implementation of OpenClaw, an AI agent framework designed to run locally on your machine. It can execute commands, control browsers, manage files, and integrate with various chat platforms — with minimal memory footprint and no runtime dependencies.

## Prerequisites

Before you start, make sure you have:

- **Linux (Ubuntu 20.04+)** or **Windows 10+ (WSL2)**
- **C++17 compatible compiler** (GCC 7+, Clang 5+)
- **CMake 3.15+**
- **Node.js 16+** (for plugin support)
- **An LLM API key** (OpenAI, Anthropic, or any compatible provider)

## Installation Methods

### Method 1: Quick Docker Setup

The fastest way to get started:

```bash
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -e OPENAI_API_KEY=sk-... \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest
```

### Method 2: Build from Source

Clone and build QuantClaw:

```bash
# Clone the repository
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw

# Install system dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake libssl-dev \
  libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev zlib1g-dev

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)

# Verify the build
./quantclaw_tests

# Install (optional)
sudo make install
```

### Method 3: Install Script

```bash
sudo bash scripts/install.sh
```

The script auto-detects your OS, installs dependencies, builds from source, and creates your workspace.

## Initial Setup

Once installed, run the onboarding wizard:

```bash
# Interactive setup wizard (recommended)
quantclaw onboard

# Or quick setup with defaults (no prompts)
quantclaw onboard --quick
```

The wizard will:
- Create `~/.quantclaw/quantclaw.json` (configuration)
- Create `~/.quantclaw/agents/main/workspace/` with all 8 workspace files
- Prompt for your LLM provider and API key
- Optionally install as a system daemon

## Your First Session

### Start the Gateway

```bash
# Run in foreground
quantclaw gateway

# Or install and start as a background service
quantclaw gateway install
quantclaw gateway start
```

### Send a Message

```bash
quantclaw agent "Hello! Introduce yourself."
```

### Open the Web Dashboard

```bash
quantclaw dashboard
```

This opens `http://127.0.0.1:18801` in your browser — a full web interface for chatting, managing sessions, and viewing configuration.

## Command Line Usage

```bash
# Send a message (creates a new session automatically)
quantclaw agent "What is the weather today?"

# Use a specific session
quantclaw agent --session my:project "Continue our discussion"

# One-shot query without session history
quantclaw eval "What is 2 + 2?"

# Check gateway status
quantclaw health
quantclaw status
```

## Configuration

The main configuration file is `~/.quantclaw/quantclaw.json`:

```json
{
  "llm": {
    "model": "openai/qwen-max",
    "maxIterations": 15,
    "maxTokens": 4096
  },
  "providers": {
    "openai": {
      "apiKey": "YOUR_API_KEY",
      "baseUrl": "https://api.openai.com/v1"
    }
  },
  "gateway": {
    "port": 18800,
    "controlUi": { "port": 18801 }
  }
}
```

See the [Configuration Guide](/guide/configuration) for all options.

## Next Steps

- 📖 [Configuration Guide](/guide/configuration)
- 🏗️ [Architecture Overview](/guide/architecture)
- 🔌 [Plugin Development](/guide/plugins)
- 🛠️ [CLI Reference](/guide/cli-reference)

## Getting Help

- **Documentation**: [Full docs](/guide/documentation)
- **GitHub Issues**: [Report bugs](https://github.com/QuantClaw/QuantClaw/issues)
- **Discussions**: [Community support](https://github.com/QuantClaw/QuantClaw/discussions)

## Troubleshooting

### Build Errors

```bash
# Check system dependencies
sudo apt install build-essential cmake libssl-dev \
  libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev

# Rebuild with verbose output
cd build
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON
make -j$(nproc)
```

### Gateway Won't Start

```bash
quantclaw config get gateway.port   # Check configured port
quantclaw doctor                    # Run full diagnostics
```

### API Key Problems

```bash
quantclaw health                    # Check gateway connectivity
quantclaw config get llm.model      # Verify model/provider config
```

---

🎉 **You're ready!** Check out the [feature guide](/guide/features) to explore more capabilities.
