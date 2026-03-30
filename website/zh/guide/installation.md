# 安装说明

不同平台和使用场景的详细安装指南。

## 系统要求

### 最低要求

- **CPU**：2 核，2+ GHz
- **RAM**：4 GB
- **磁盘**：2 GB 可用空间
- **OS**：Linux（Ubuntu 20.04+）、Windows（WSL2）或 macOS 13+（官方支持 Apple Silicon）

### 推荐配置

- **CPU**：4+ 核
- **RAM**：8+ GB
- **磁盘**：10 GB 可用空间

## Linux 安装

### Ubuntu / Debian

#### 从源码编译

```bash
# 安装编译依赖
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  git \
  libssl-dev \
  libcurl4-openssl-dev \
  nlohmann-json3-dev \
  libspdlog-dev \
  zlib1g-dev

# 克隆仓库
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw

# 编译
mkdir build && cd build
cmake ..
cmake --build . --parallel

# 可选：系统级安装
sudo cmake --install .

# 运行测试
./quantclaw_tests
```

#### 使用 Docker

```bash
# 先构建镜像（参见 scripts/ 目录）
VERSION=$(cat scripts/DOCKER_VERSION)
docker build -f scripts/Dockerfile -t quantclaw:$VERSION -t quantclaw:latest .

# 运行容器
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -e OPENAI_API_KEY=sk-... \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest

# 查看日志
docker logs quantclaw
```

### Fedora / CentOS / RHEL

```bash
# 安装依赖
sudo dnf groupinstall "Development Tools" -y
sudo dnf install cmake openssl-devel spdlog-devel -y

# 从源码编译
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
mkdir build && cd build
cmake ..
cmake --build . --parallel
sudo cmake --install .
```

### Arch Linux

```bash
# 从源码编译
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
mkdir build && cd build
cmake ..
cmake --build . --parallel
sudo cmake --install .
```

## Windows 安装

### Windows 10/11（WSL2，推荐）

#### 设置 WSL2

```powershell
# 启用 WSL2
wsl --install
```

#### 在 WSL2 中安装

在 WSL2 终端中按照 Ubuntu/Linux 步骤操作：

```bash
wsl
cd ~
git clone https://github.com/QuantClaw/QuantClaw.git
# ... 按照 Linux 编译步骤继续
```

运行后，通过 `http://localhost:18801` 访问 Web 仪表板。

### 原生 Windows（MSVC）

**前置条件：**
- Visual Studio 2019+ 或 Build Tools for Visual Studio
- CMake 3.15+
- Git for Windows

```batch
REM 克隆仓库
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw

REM 创建构建目录
mkdir build
cd build

REM 生成 Visual Studio 项目
cmake .. -G "Visual Studio 17 2022"

REM 编译
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%

REM 测试
Release\quantclaw_tests.exe
```

**依赖（vcpkg）：**

```batch
vcpkg install nlohmann-json openssl spdlog zlib
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
```

#### Gateway 服务设置（解决权限问题）

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
# ... 按照 Linux 编译步骤继续
```

#### 常见问题

**Q: `gateway install` 命令失败？**

A: Windows 上的 `gateway install` 当前仅创建后台进程，不会自动创建计划任务。请使用上述方案一或方案二。

**Q: 端口被占用？**

A: 修改配置文件 `~\.quantclaw\quantclaw.json` 中的 `gateway.port` 值。

**Q: 杀毒软件拦截？**

A: 将 `quantclaw.exe` 添加到杀毒软件白名单。

> **Windows 兼容性说明：**
> - `NOMINMAX` 会自动定义，避免 Windows API 宏与 `std::min`/`std::max` 冲突。
> - `bcrypt` 会在 Windows 上自动链接，用于满足底层加密/TLS 库（例如 mbedtls）相关的依赖。
> - 未找到 ZLIB 时，`HTTPLIB_REQUIRE_ZLIB` 会显式设为 `OFF`，防止 CMake 缓存导致链接失败。
> - Windows 不支持 `logs -f`（实时跟踪）；请使用 `logs -n <行数>` 查看最近日志。

## macOS 安装

### 推荐：安装脚本

```bash
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
bash scripts/install.sh --user
```

这会把 `quantclaw` 安装到 `~/.quantclaw/bin`，执行 `onboard --quick`，并把 launchd 服务定义写到 `~/Library/LaunchAgents/com.quantclaw.gateway.plist`。

### 从源码编译（手动）

```bash
# build.sh 支持的 Homebrew 依赖
brew install cmake ninja pkg-config git spdlog openssl@3 curl node

# 编译
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw

# 推荐：使用脚本构建
./scripts/build.sh --tests

# 运行测试
ctest --test-dir build --output-on-failure

# 可选：安装后台服务定义
./build/quantclaw gateway install
```

如果你更想手动执行 CMake，可显式传入 Homebrew 前缀：

```bash
cmake -B build -G Ninja \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DCURL_ROOT="$(brew --prefix curl)"
cmake --build build --parallel
```

## 安装后设置

### 初始化配置

```bash
# 交互式设置（推荐）
quantclaw onboard

# 快速设置（使用默认值）
quantclaw onboard --quick
```

初始化过程中会创建配置文件、工作空间和网关认证 token。Provider 的 API Key 请随后手动写入 `~/.quantclaw/quantclaw.json`。

### 验证安装

```bash
# 检查版本
quantclaw --version

# 运行诊断
quantclaw doctor

# 测试基本功能（需先启动网关）
quantclaw gateway &
quantclaw agent "你好，你是谁？"
```

### 配置环境变量（可选）

```bash
export QUANTCLAW_LOG_LEVEL=debug
export QUANTCLAW_GATEWAY_PORT=18800
```

## 更新 QuantClaw

### 从源码更新

```bash
cd QuantClaw
git pull origin main
cd build
cmake --build . --parallel
sudo cmake --install .
```

### Docker 更新

```bash
# 重新构建镜像
docker build -f scripts/Dockerfile -t quantclaw:latest .
docker stop quantclaw && docker rm quantclaw

# 使用新镜像重新运行
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -e OPENAI_API_KEY=sk-... \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest
```

## 故障排除

### 编译失败

**找不到 CMake**

```bash
sudo apt-get install cmake       # Ubuntu
brew install cmake               # macOS
```

**缺少依赖**

```bash
# Ubuntu
sudo apt-get install libssl-dev libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev

# macOS
brew install openssl nlohmann-json spdlog
```

### 运行时问题

**端口被占用**

```bash
# 检查占用 18800 端口的进程
lsof -i :18800
kill -9 <PID>

# 或修改配置文件中的端口
quantclaw config set gateway.port 18810
```

**权限拒绝**

```bash
# 确保 ~/.quantclaw 可写
chmod 700 ~/.quantclaw
```

**配置问题**

```bash
quantclaw config validate
quantclaw config schema
```

## 卸载

### 二进制安装

```bash
sudo rm /usr/local/bin/quantclaw

# 可选：删除配置和数据
rm -rf ~/.quantclaw
```

### Docker

```bash
docker stop quantclaw
docker rm quantclaw
docker rmi quantclaw:latest
docker volume rm quantclaw_data
```

---

**下一步**：[配置你的安装](/zh/guide/configuration) 或[开始运行 Agent](/zh/guide/getting-started)。
