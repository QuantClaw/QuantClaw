# 安装说明

不同平台和使用场景的详细安装指南。

## 系统要求

### 最低要求

- **CPU**：2 核，2+ GHz
- **RAM**：4 GB
- **磁盘**：2 GB 可用空间
- **OS**：Linux（Ubuntu 20.04+）、Windows（WSL2）或 macOS（10.15+）

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
make -j$(nproc)

# 可选：系统级安装
sudo make install

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
make -j$(nproc)
sudo make install
```

### Arch Linux

```bash
# 从源码编译
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
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

## macOS 安装

### 从源码编译

```bash
# 安装依赖（Homebrew）
brew install cmake openssl nlohmann-json spdlog

# 编译
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw
mkdir build && cd build
cmake -DOPENSSL_DIR=$(brew --prefix openssl) ..
cmake --build . -j $(sysctl -n hw.ncpu)

# 运行测试
./quantclaw_tests

# 安装
sudo cmake --install .
```

## 安装后设置

### 初始化配置

```bash
# 交互式设置（推荐）
quantclaw onboard

# 快速设置（使用默认值）
quantclaw onboard --quick
```

设置过程中需要配置：
- **API Key**：LLM Provider 凭据
- **默认模型**：主要 LLM 模型（格式：`provider/model-name`）
- **工作空间**：文件和记忆的存储位置

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
make -j$(nproc)
sudo make install
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
