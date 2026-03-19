# Building from Source

Complete guide to building QuantClaw from source code.

## Prerequisites

### All Platforms

- **Git** - Version control
- **CMake** - Build system (3.15+)
- **C++17 Compiler** - GCC 7+, Clang 6+, or MSVC 2017+
- **Node.js** - 16+ (for plugin system)

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install -y \
  build-essential \
  cmake \
  git \
  libssl-dev \
  nlohmann-json3-dev \
  libspdlog-dev \
  pkg-config
```

### macOS

```bash
# Homebrew
brew install cmake openssl nlohmann-json spdlog

# Or MacPorts
sudo port install cmake openssl nlohmann_json spdlog
```

### Windows (MSVC)

- **Visual Studio 2019+** or **Build Tools for Visual Studio**
- **CMake 3.15+**
- **Git for Windows**

> **Note:** The build system automatically defines `NOMINMAX` and links against `bcrypt` on Windows to avoid conflicts with Windows API macros and satisfy required cryptography library dependencies. No manual configuration is required.

## Clone Repository

```bash
git clone https://github.com/QuantClaw/quantclaw.git
cd quantclaw

# Optional: Check out specific version
git checkout v1.0.0
```

## Build Steps

### Linux/macOS

```bash
# Create build directory
mkdir build && cd build

# Configure build
cmake ..

# Build (use multiple cores for speed)
cmake --build . -j$(nproc)

# Optional: Install system-wide
sudo cmake --install .
```

### Windows (Command Prompt)

```batch
# Create build directory
mkdir build
cd build

# Configure for Visual Studio
cmake .. -G "Visual Studio 17 2022"

# Build
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%

# Optional: Install
cmake --install . --config Release
```

### Windows (PowerShell)

```powershell
# Create and enter build directory
New-Item -ItemType Directory -Name build -Force
cd build

# Configure and build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release -j $env:NUMBER_OF_PROCESSORS
```

## Build Verification

```bash
# Run tests
./quantclaw_tests

# Or on Windows
.\Release\quantclaw_tests.exe

# Check installation
quantclaw --version
```

## Build Options

### Standard Options

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

**Build Types:**
- `Release` - Optimized for performance
- `Debug` - Symbols and debugging info
- `RelWithDebInfo` - Release with debug symbols
- `MinSizeRel` - Optimized for size

### Feature Flags

```bash
# Disable tests
cmake -DBUILD_TESTS=OFF ..

# Disable sidecar (Node.js plugin system)
cmake -DWITH_SIDECAR=OFF ..

# Enable TLS support
cmake -DWITH_TLS=ON ..

# Enable Avro serialization
cmake -WITH_AVRO=ON ..
```

### Compiler-Specific Options

**GCC**
```bash
cmake -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc ..
```

**Clang**
```bash
cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang ..
```

**MSVC**
```bash
cmake .. -G "Visual Studio 17 2022" -DCMAKE_CXX_COMPILER=cl
```

### Custom Installation Path

```bash
cmake -DCMAKE_INSTALL_PREFIX=/custom/path ..
cmake --build .
cmake --install .
```

## Advanced Build Configuration

### Enable Sanitizers (for debugging)

```bash
# Memory sanitizer
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address" ..

# Thread sanitizer
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..

# Undefined behavior sanitizer
cmake -DCMAKE_CXX_FLAGS="-fsanitize=undefined" ..
```

### Optimize for Size

```bash
cmake -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_CXX_FLAGS="-Os" ..
```

### Optimize for Performance

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=native" ..
```

### Verbose Build Output

```bash
cmake --build . -v
# Or for traditional make
make VERBOSE=1
```

## Dependency Management

### Automatic Dependency Download

By default, CMake downloads missing dependencies:

```bash
cmake ..
# Downloads: Google Test, IXWebSocket, etc.
```

### Manual Dependency Management

Install system dependencies:

```bash
# Ubuntu
sudo apt-get install libssl-dev nlohmann-json3-dev libspdlog-dev

# Then configure with system packages
cmake -DUSE_SYSTEM_LIBS=ON ..
```

### Custom Dependency Paths

```bash
cmake -DOPENSSL_DIR=/path/to/openssl \
      -DNLOHMANN_JSON_DIR=/path/to/nlohmann-json ..
```

## Cross-Compilation

### Linux to Windows (MinGW)

```bash
# Install MinGW cross-compiler
sudo apt-get install mingw-w64

# Configure for cross-compilation
cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/mingw-toolchain.cmake ..
cmake --build .
```

### Linux to macOS (Clang)

```bash
# Requires Apple's Clang and SDKs
cmake -DCMAKE_SYSTEM_NAME=Darwin \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 ..
```

## Building with Docker

### Using Official Docker Image

```bash
docker run -v $(pwd):/workspace quantclaw:build-env \
  bash -c "cd /workspace && \
           mkdir build && cd build && \
           cmake .. && \
           cmake --build . -j$(nproc)"
```

### Building Docker Image

```bash
docker build -f Dockerfile -t quantclaw:latest .

# Run in container
docker run -it quantclaw:latest quantclaw --version
```

## Troubleshooting Build Issues

### CMake Not Found

```bash
# Install CMake
# Ubuntu
sudo apt-get install cmake

# macOS
brew install cmake

# Windows - Download from cmake.org
```

### Missing Dependencies

```bash
# Check what's missing
cmake --debug-output ..

# Install missing packages
# Ubuntu example:
sudo apt-get install nlohmann-json3-dev libspdlog-dev
```

### Compiler Errors

**C++17 Not Supported**
```bash
# Ensure C++17 compiler
cmake -DCMAKE_CXX_STANDARD=17 ..

# Or upgrade compiler
# Ubuntu: sudo apt-get install g++-11
# macOS: brew install gcc
```

**OpenSSL Not Found**
```bash
# Linux
sudo apt-get install libssl-dev

# macOS
brew install openssl
cmake -DOPENSSL_DIR=$(brew --prefix openssl) ..

# Windows - Download from openssl.org
```

**JSON Library Not Found**
```bash
# Ubuntu
sudo apt-get install nlohmann-json3-dev

# macOS
brew install nlohmann-json

# Or download header-only from github
```

**ZLIB / HTTPLIB compression errors**

If your system has ZLIB installed in some configurations but not others (e.g., a fresh Docker image or minimal CI environment), CMake may cache a stale `HTTPLIB_REQUIRE_ZLIB=ON` value from a previous configure run. The build system now explicitly forces `HTTPLIB_REQUIRE_ZLIB` to `OFF` when ZLIB is not found, but if you see linker errors related to `z` or `zlib`, clear the CMake cache and reconfigure:

```bash
# Linux/macOS
rm -rf build && mkdir build && cd build
cmake ..

# Windows (PowerShell)
Remove-Item -Recurse -Force build; mkdir build; cd build
cmake ..

# Windows (cmd)
rmdir /s /q build && mkdir build && cd build && cmake ..
```

### Build Timeout

```bash
# Reduce parallel jobs
cmake --build . -j2

# Or use fewer resources
make -j1
```

### Out of Memory

```bash
# Reduce parallel compilation
cmake --build . -j1

# Or increase swap space
# Linux: swapoff -a && dd if=/dev/zero of=/swapfile bs=1G count=4 && swapon /swapfile
```

## Running Tests

### All Tests

```bash
cd build
./quantclaw_tests

# Or with cmake
cmake --build . --target test
ctest --verbose
```

### Specific Test

```bash
./quantclaw_tests --gtest_filter="TestName*"

# List available tests
./quantclaw_tests --gtest_list_tests
```

### Coverage Report

```bash
cmake -DENABLE_COVERAGE=ON ..
cmake --build .
ctest
# Generate coverage report
```

## Performance Profiling

### Build with Profiling

```bash
cmake -DENABLE_PROFILING=ON ..
cmake --build .

# Run and profile
perf record ./quantclaw agent
perf report
```

### Memory Profiling

```bash
# Valgrind
valgrind --leak-check=full ./quantclaw agent

# Google Perftools
cmake -WITH_PERFTOOLS=ON ..
```

## Clean Build

```bash
# Remove build artifacts
rm -rf build

# Or with git
git clean -xfd build/

# Rebuild from scratch
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

## Development Workflow

### Incremental Compilation

```bash
# After code changes, rebuild only affected files
cmake --build . -j$(nproc)

# Don't do `make clean` unless necessary
```

### Quick Test Cycle

```bash
# Edit code
# Build and test
cmake --build . && ./quantclaw_tests

# Or use a file watcher
find src include -name "*.cpp" -o -name "*.hpp" | \
  entr bash -c "cmake --build build && ./build/quantclaw_tests"
```

### Git Workflow

```bash
# Create feature branch
git checkout -b feature/my-feature

# Make changes and commit
git add .
git commit -m "Add feature"

# Push to fork
git push origin feature/my-feature

# Create pull request on GitHub
```

## Installing Build Dependencies from Source

If package manager versions are too old:

### Install CMake from Source

```bash
wget https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0.tar.gz
tar xzf cmake-3.28.0.tar.gz && cd cmake-3.28.0
./bootstrap && make -j$(nproc) && sudo make install
```

### Install Modern GCC

```bash
# Ubuntu
sudo apt-get install build-essential software-properties-common
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install gcc-13 g++-13
```

## Contributing

When contributing code:

1. **Follow style guide**: See `.clang-format`
2. **Write tests**: Add tests for new features
3. **Run full build**: `cmake --build . && ./quantclaw_tests`
4. **Format code**: `clang-format -i file.cpp`
5. **Check compliance**: `cmake --build . --target clang-tidy`

---

**Next**: [View architecture](/guide/architecture) or [develop plugins](/guide/plugins).
