#!/bin/bash
set -e

echo "Installing QuantClaw..."

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    echo "Cannot detect OS"
    exit 1
fi

# Install dependencies
echo "Installing dependencies..."
case $OS in
    ubuntu|debian)
        apt-get update
        apt-get install -y \
            build-essential \
            cmake \
            git \
            libssl-dev \
            libcurl4-openssl-dev \
            nlohmann-json3-dev \
            libspdlog-dev \
            zlib1g-dev
        ;;
    fedora|centos|rhel)
        dnf install -y \
            gcc gcc-c++ \
            cmake \
            git \
            openssl-devel \
            libcurl-devel \
            nlohmann_json-devel \
            spdlog-devel \
            zlib-devel
        ;;
    arch|manjaro)
        pacman -S --noconfirm \
            base-devel \
            cmake \
            git \
            openssl \
            curl \
            nlohmann-json \
            spdlog \
            zlib
        ;;
    *)
        echo "Unsupported OS: $OS"
        exit 1
        ;;
esac

# Build from source (IXWebSocket fetched via FetchContent)
echo "Building QuantClaw..."
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF ..
make -j$(nproc)

# Install
echo "Installing binary..."
cp quantclaw /usr/local/bin/
chmod +x /usr/local/bin/quantclaw

# Create workspace (OpenClaw-compatible layout)
echo "Creating workspace..."
USER_HOME=$(eval echo ~$SUDO_USER)
mkdir -p "$USER_HOME/.quantclaw/agents/main/workspace"
mkdir -p "$USER_HOME/.quantclaw/agents/main/sessions"
mkdir -p "$USER_HOME/.quantclaw/logs"

# Create example config (OpenClaw format)
if [ ! -f "$USER_HOME/.quantclaw/quantclaw.json" ]; then
    echo "Creating example config..."
    cat > "$USER_HOME/.quantclaw/quantclaw.json" << 'EOF'
{
  "agent": {
    "model": "openai/qwen-max",
    "maxIterations": 15,
    "temperature": 0.7
  },
  "gateway": {
    "port": 18789,
    "bind": "loopback",
    "auth": { "mode": "token" },
    "controlUi": { "enabled": true }
  },
  "providers": {
    "openai": {
      "apiKey": "YOUR_API_KEY_HERE",
      "baseUrl": "https://api.openai.com/v1"
    }
  },
  "channels": {
    "discord": {
      "enabled": false,
      "token": "YOUR_DISCORD_TOKEN_HERE"
    }
  },
  "tools": {
    "allow": ["group:fs", "group:runtime"],
    "deny": []
  }
}
EOF
fi

# Fix ownership
if [ -n "$SUDO_USER" ]; then
    chown -R "$SUDO_USER:$(id -gn $SUDO_USER)" "$USER_HOME/.quantclaw"
fi

echo ""
echo "QuantClaw installed successfully!"
echo ""
echo "Next steps:"
echo "1. Edit ~/.quantclaw/quantclaw.json with your API keys"
echo "2. Start gateway: quantclaw gateway"
echo "3. Or install as service: quantclaw gateway install"
echo "4. Send a message: quantclaw agent -m \"Hello!\""
echo "5. Check status: quantclaw status"
echo "6. Run diagnostics: quantclaw doctor"
