# Installation Guide

Detailed installation instructions for different platforms and use cases.

## System Requirements

### Minimum Requirements
- **CPU**: 2 cores, 2+ GHz
- **RAM**: 4 GB
- **Disk**: 2 GB free space
- **OS**: Linux (Ubuntu 20.04+), Windows (native or WSL2), or macOS 13+ on Apple Silicon

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
cmake --build . --parallel

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
cmake --build . --parallel
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
cmake --build . --parallel
sudo cmake --install .
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

#### Gateway Service Setup (解决权限问题)

Windows 环境下，Gateway 服务启动需要特殊处理：

**方案一：使用计划任务（推荐）**

```powershell
# 以管理员身份运行 PowerShell
cd path\to\QuantClaw
powershell -ExecutionPolicy Bypass -File scripts\gateway-setup-windows.ps1
```

此脚本会：
- ✅ 创建计划任务（开机自动启动）
- ✅ 生成启动辅助脚本
- ✅ 创建默认配置文件
- ✅ 设置日志目录

**方案二：手动启动（无需管理员权限）**

```batch
REM 双击运行或在命令行执行
scripts\gateway-manual.bat

REM 或直接运行
build\Release\quantclaw.exe gateway run
```

**方案三：WSL2 替代方案**

如果遇到 Windows 权限问题，推荐使用 WSL2：

```powershell
# 安装 WSL2
wsl --install

# 在 WSL2 中使用 Linux 安装方式
wsl
cd ~/QuantClaw
# ... follow Linux build steps
```

#### 常见问题

**Q: `gateway install` 命令失败？**

A: Windows 上的 `gateway install` 当前仅创建后台进程，不会自动创建计划任务。请使用上述方案一或方案二。

**Q: 端口被占用？**

A: 修改配置文件 `~\.quantclaw\quantclaw.json` 中的 `gateway.port` 值。

**Q: 杀毒软件拦截？**

A: 将 `quantclaw.exe` 添加到杀毒软件白名单。

#### Windows Compatibility Notes

> - `NOMINMAX` is defined automatically to prevent conflicts between Windows API macros and C++ `std::min`/`std::max`.
> - `bcrypt` is linked automatically to satisfy Windows crypto/TLS library dependencies (for example, mbedtls).
> - `HTTPLIB_REQUIRE_ZLIB` is explicitly set to `OFF` when ZLIB is not present, preventing stale CMake cache issues on minimal environments.
> - The `logs -f` (follow) flag is not supported on Windows; use `logs -n <count>` to view recent entries.

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

### Recommended: install script

```bash
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
bash scripts/install.sh --user
```

This installs `quantclaw` into `~/.quantclaw/bin`, runs `onboard --quick`, and writes the launchd service definition to `~/Library/LaunchAgents/com.quantclaw.gateway.plist`.

### Build from Source (manual)

#### Install Dependencies
```bash
# Homebrew packages used by the supported build script
brew install cmake ninja pkg-config git spdlog openssl@3 curl node
```

#### Build
```bash
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw

# Recommended scripted build
./scripts/build.sh --tests

# Run tests
ctest --test-dir build --output-on-failure

# Optional: install the background service definition
./build/quantclaw gateway install
```

If you prefer manual CMake configuration on macOS, pass the Homebrew prefixes explicitly:

```bash
brew install openssl@3 curl
cmake -B build -G Ninja \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DCURL_ROOT="$(brew --prefix curl)"
cmake --build build --parallel
```

## Post-Installation Setup

### Initialize Configuration

```bash
# Interactive setup (recommended)
quantclaw onboard

# Quick setup (use defaults)
quantclaw onboard --quick
```

During setup, QuantClaw creates the config file, workspace, and gateway auth token. Add your provider API keys afterwards by editing `~/.quantclaw/quantclaw.json`.

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
cmake --build . --parallel
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
