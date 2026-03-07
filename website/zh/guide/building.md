# 从源码构建

完整的源码构建指南。

## 前置条件

### 所有平台

- **Git** — 版本控制
- **CMake** — 构建系统（3.15+）
- **C++17 编译器** — GCC 7+、Clang 5+ 或 MSVC 2019+
- **Node.js** — 16+（插件系统需要）

### Linux（Ubuntu / Debian）

```bash
sudo apt-get install -y \
  build-essential \
  cmake \
  git \
  libssl-dev \
  libcurl4-openssl-dev \
  nlohmann-json3-dev \
  libspdlog-dev \
  zlib1g-dev
```

### macOS

```bash
# Homebrew
brew install cmake openssl nlohmann-json spdlog

# 或 MacPorts
sudo port install cmake openssl nlohmann_json spdlog
```

### Windows（MSVC）

- Visual Studio 2019+ 或 Build Tools for Visual Studio
- CMake 3.15+
- Git for Windows

## 克隆仓库

```bash
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw

# 可选：切换到指定版本
git checkout v0.3.0-alpha
```

## 构建步骤

### Linux / macOS

```bash
mkdir build && cd build

# 标准构建
cmake ..
make -j$(nproc)

# Debug 构建
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# 指定编译器
cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
make -j$(nproc)
```

### 使用 build.sh（推荐）

项目提供了智能构建脚本：

```bash
# 标准构建
./scripts/build.sh

# Debug 构建
./scripts/build.sh --debug

# 构建并运行测试
./scripts/build.sh --tests

# 清理后重新构建
./scripts/build.sh -c

# 启用 AddressSanitizer
./scripts/build.sh --asan
```

### Windows（MSVC）

```batch
mkdir build
cd build

REM 生成 Visual Studio 项目
cmake .. -G "Visual Studio 17 2022"

REM 编译 Release 版本
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%
```

**vcpkg 依赖：**

```batch
vcpkg install nlohmann-json openssl spdlog zlib
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

## 运行测试

```bash
cd build

# 运行所有测试
./quantclaw_tests

# 通过 ctest 运行
ctest --output-on-failure

# 运行指定测试套件
./quantclaw_tests --gtest_filter=AgentLoopTest.*
./quantclaw_tests --gtest_filter=ConfigTest.*
```

## 构建 Docker 镜像

```bash
VERSION=$(cat scripts/DOCKER_VERSION)

# 生产镜像（三阶段构建）
docker build \
  -f scripts/Dockerfile \
  --build-arg VERSION=$VERSION \
  --build-arg BUILD_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ) \
  --build-arg VCS_REF=$(git rev-parse --short HEAD) \
  -t quantclaw:$VERSION \
  -t quantclaw:latest \
  .

# 测试镜像
docker build -f scripts/Dockerfile.test -t quantclaw-test:$VERSION .

# 开发镜像（含 gdb/valgrind）
docker build -f scripts/Dockerfile.dev -t quantclaw-dev:$VERSION .
```

## 安装

```bash
# 安装到系统（/usr/local/bin）
sudo cmake --install build/

# 或从 build 目录
cd build && sudo make install
```

## 代码格式化

```bash
# 格式化所有 C++ 源文件
./scripts/format-code.sh

# Dry-run（CI 使用）
./scripts/format-code.sh --check

# 在 Docker 中格式化（无需本地 clang-format）
./scripts/format-code-docker.sh
```

## 持续集成

CI 配置位于 `.github/workflows/github-actions.yml`，在每次 PR 时自动运行：

- C++ 代码格式检查（clang-format）
- 完整 C++ 测试套件（886 项测试）
- Sidecar TypeScript 测试

## 故障排除

### CMake 找不到依赖

```bash
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON
```

### 链接错误（OpenSSL）

```bash
# Ubuntu
sudo apt-get install libssl-dev

# macOS
cmake .. -DOPENSSL_DIR=$(brew --prefix openssl)
```

### 编译器版本过低

```bash
# 安装 GCC 11
sudo apt-get install gcc-11 g++-11
cmake .. -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11
```

---

**下一步**：[参与贡献](/zh/guide/contributing) 或[查看 CLI 参考](/zh/guide/cli-reference)。
