# Installation Guide

Detailed installation instructions for different platforms and use cases.

## System Requirements

### Minimum Requirements
- **CPU**: 2 cores, 2+ GHz
- **RAM**: 4 GB
- **Disk**: 2 GB free space
- **OS**: Linux (Ubuntu 20.04+), Windows (WSL2), or macOS (10.15+)

### Recommended Requirements
- **CPU**: 4+ cores
- **RAM**: 8+ GB
- **Disk**: 10 GB free space
- **GPU**: Optional (for faster inference if supported by your LLM provider)

## Linux Installation

### Ubuntu/Debian

#### Using Pre-built Binary
```bash
# Download the latest binary
wget https://github.com/QuantClaw/QuantClaw/releases/download/v1.0.0/quantclaw-linux-x64.tar.gz

# Extract
tar xzf quantclaw-linux-x64.tar.gz

# Install to system path
sudo mv quantclaw /usr/local/bin/
chmod +x /usr/local/bin/quantclaw

# Verify
quantclaw --version
```

#### Build from Source
```bash
# Install build dependencies
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  git \
  libssl-dev \
  nlohmann-json3-dev \
  libspdlog-dev

# Clone repository
git clone https://github.com/QuantClaw/QuantClaw.git
cd quantclaw

# Build
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Optional: Install system-wide
sudo cmake --install .

# Run tests
./quantclaw_tests
```

#### Using Docker
```bash
# Pull image
docker pull quantclaw:latest

# Run container
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest

# View logs
docker logs quantclaw
```

### Fedora/CentOS/RHEL

```bash
# Install dependencies
sudo dnf groupinstall "Development Tools" -y
sudo dnf install cmake openssl-devel nlohmann_json-devel spdlog-devel -y

# Build from source
git clone https://github.com/QuantClaw/QuantClaw.git
cd quantclaw
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
sudo cmake --install .
```

### Arch Linux

```bash
# Install from AUR (if available)
yay -S quantclaw

# Or build from source
git clone https://github.com/QuantClaw/QuantClaw.git
cd quantclaw
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

## Windows Installation

### Windows 10/11 with WSL2

#### Setup WSL2
```powershell
# Enable WSL2
wsl --install

# Or if already installed
wsl --set-default-version 2
```

#### Install in WSL2
Follow the Ubuntu/Linux instructions above within WSL2:
```bash
wsl
cd ~
git clone https://github.com/QuantClaw/QuantClaw.git
# ... follow Linux build steps
```

#### Run from Windows
```powershell
# Forward WSL2 port to Windows
# In WSL2 terminal:
quantclaw gateway

# In PowerShell (Windows):
# open http://localhost:18801
```

### Native Windows (MSVC)

#### Prerequisites
- **Visual Studio 2019+** or **Build Tools for Visual Studio**
- **CMake 3.15+**
- **Git**

#### Build Process
```batch
REM Clone repository
git clone https://github.com/QuantClaw/QuantClaw.git
cd quantclaw

REM Create build directory
mkdir build
cd build

REM Generate Visual Studio project
cmake .. -G "Visual Studio 17 2022"

REM Build
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%

REM Tests
Release\quantclaw_tests.exe
```

#### Install Dependencies (vcpkg)
```batch
REM If CMake can't find dependencies
vcpkg install nlohmann-json openssl spdlog zlib

REM Then configure with toolchain
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
```

### Docker on Windows
```powershell
# Pull and run Docker container
docker pull quantclaw:latest

docker run -d `
  --name quantclaw `
  -p 18800:18800 `
  -p 18801:18801 `
  -v quantclaw_data:/home/quantclaw/.quantclaw `
  quantclaw:latest

# View logs
docker logs quantclaw

# Stop container
docker stop quantclaw
```

## macOS Installation

### Using Homebrew (if tap available)
```bash
brew tap quantclaw/quantclaw
brew install quantclaw
```

### Build from Source

#### Install Dependencies
```bash
# Using Homebrew
brew install cmake openssl nlohmann-json spdlog

# Or using MacPorts
sudo port install cmake openssl nlohmann_json spdlog
```

#### Build
```bash
git clone https://github.com/QuantClaw/QuantClaw.git
cd quantclaw

mkdir build && cd build
cmake -DOPENSSL_DIR=$(brew --prefix openssl) ..
cmake --build . -j $(sysctl -n hw.ncpu)

# Run tests
./quantclaw_tests

# Install
sudo cmake --install .
```

## Post-Installation Setup

### Initialize Configuration

```bash
# Interactive setup (recommended)
quantclaw onboard

# Quick setup (use defaults)
quantclaw onboard --quick
```

During setup, you'll configure:
- **API Keys**: LLM provider credentials
- **Default Model**: Primary LLM model
- **Workspace**: Location for files and memory
- **Plugins**: Initial plugin selection

### Verify Installation

```bash
# Check version
quantclaw --version

# Run tests
quantclaw status

# Test basic functionality
quantclaw run "Hello, what's your name?"
```

### Configure Environment (Optional)

```bash
# Set default agent
export QUANTCLAW_AGENT_ID=main

# Set configuration directory
export QUANTCLAW_CONFIG_DIR=~/.quantclaw

# Set log level
export QUANTCLAW_LOG_LEVEL=debug

# Set gateway port
export QUANTCLAW_GATEWAY_PORT=18800
```

## Updating QuantClaw

### From Binary
```bash
# Download new version
wget https://github.com/QuantClaw/QuantClaw/releases/download/v1.1.0/quantclaw-linux-x64.tar.gz

# Backup old binary
cp /usr/local/bin/quantclaw /usr/local/bin/quantclaw.backup

# Install new version
tar xzf quantclaw-linux-x64.tar.gz
sudo mv quantclaw /usr/local/bin/

# Verify
quantclaw --version
```

### From Source
```bash
cd quantclaw
git pull origin main
cd build
cmake --build . -j$(nproc)
sudo cmake --install .
```

### From Homebrew
```bash
brew update
brew upgrade quantclaw
```

### Docker
```bash
docker pull quantclaw:latest
docker stop quantclaw
docker rm quantclaw

# Re-run with new image
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest
```

## Troubleshooting

### Build Failures

**CMake not found**
```bash
# Ubuntu
sudo apt-get install cmake

# macOS
brew install cmake

# Windows
choco install cmake
```

**Missing dependencies**
```bash
# Ubuntu
sudo apt-get install libssl-dev nlohmann-json3-dev libspdlog-dev

# macOS
brew install openssl nlohmann-json spdlog
```

**C++ compiler issues**
```bash
# Update compiler
sudo apt-get install build-essential  # Ubuntu/Debian
brew install gcc                       # macOS
```

### Runtime Issues

**Port already in use**
```bash
# Change gateway port
quantclaw gateway --port 9000

# Or find and kill process using port 18800
lsof -i :18800
kill -9 <PID>
```

**Permission denied errors**
```bash
# Ensure ~/.quantclaw is writable
chmod 700 ~/.quantclaw
chmod 600 ~/.quantclaw/*
```

**Configuration issues**
```bash
# Validate configuration
quantclaw config validate

# View schema
quantclaw config schema

# Reset to defaults
quantclaw onboard --reset
```

## Uninstallation

### Binary Installation
```bash
# Remove binary
sudo rm /usr/local/bin/quantclaw

# Remove configuration (optional)
rm -rf ~/.quantclaw
```

### Docker
```bash
docker stop quantclaw
docker rm quantclaw
docker rmi quantclaw:latest
```

### From Source
```bash
cd quantclaw/build
sudo cmake --uninstall

# Or manually
sudo rm /usr/local/bin/quantclaw
sudo rm -rf /usr/local/include/quantclaw
```

---

**Next**: [Configure your installation](/guide/configuration) or [get started running agents](/guide/getting-started).
