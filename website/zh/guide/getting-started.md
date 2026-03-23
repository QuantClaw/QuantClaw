# 快速开始

欢迎使用 QuantClaw！本指南帮你在几分钟内完成安装并运行。

## QuantClaw 是什么？

QuantClaw 是 OpenClaw 的 C++17 高性能实现——一个本地运行的 AI Agent 框架，可执行命令、控制浏览器、管理文件，并接入多种聊天平台，内存占用极低，无运行时依赖。

## 前置条件

- **Linux（Ubuntu 20.04+）** 或 **Windows 10+（WSL2）**
- **C++17 编译器**（GCC 7+ 或 Clang 5+）
- **CMake 3.15+**
- **Node.js 16+**（插件支持需要）
- **LLM API Key**（OpenAI、Anthropic 或任何兼容 Provider）

## 安装方式

### 方式一：从源码编译

```bash
# 克隆仓库
git clone https://github.com/QuantClaw/QuantClaw.git
cd QuantClaw

# 安装系统依赖（Ubuntu/Debian）
sudo apt install build-essential cmake libssl-dev \
  libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev zlib1g-dev

# 编译
mkdir build && cd build
cmake ..
make -j$(nproc)

# 验证编译
./quantclaw_tests

# 安装（可选）
sudo make install
```

### 方式二：一键安装脚本

```bash
sudo bash scripts/install.sh
```

脚本会自动检测系统（Ubuntu/Debian/Fedora/Arch），安装依赖、编译源码并创建工作空间。

### 方式三：Docker

```bash
docker run -d \
  --name quantclaw \
  -p 18800:18800 \
  -p 18801:18801 \
  -e OPENAI_API_KEY=sk-... \
  -v quantclaw_data:/home/quantclaw/.quantclaw \
  quantclaw:latest
```

## 初始化设置

安装完成后，运行 Onboarding 向导：

```bash
# 交互式设置向导（推荐）
quantclaw onboard

# 或快速设置（无提示）
quantclaw onboard --quick
```

向导会：
- 创建 `~/.quantclaw/quantclaw.json`（配置文件）
- 创建 `~/.quantclaw/agents/main/workspace/`（含全部 8 个工作空间文件）
- 引导填写 LLM Provider 和 API Key
- 可选安装为系统守护进程

## 第一次对话

### 启动网关

```bash
# 前台运行
quantclaw gateway

# 或安装为后台服务
quantclaw gateway install
quantclaw gateway start
```

### 发送消息

```bash
quantclaw agent "你好！介绍一下你自己。"
```

### 打开 Web 仪表板

```bash
quantclaw dashboard
```

在浏览器中打开 `http://127.0.0.1:18801`——包含聊天、会话管理和配置界面。

## 仪表板认证

仪表板需要输入 token 才能访问。Token 在配置文件中设置：

```json
{
  "gateway": {
    "auth": {
      "mode": "token",
      "token": "YOUR_SECRET_TOKEN"
    }
  }
}
```

**首次访问：**
1. 在浏览器中打开 `http://127.0.0.1:18801`
2. 输入你在 `~/.quantclaw/quantclaw.json` 中配置的 token
3. Token 会保存在浏览器 localStorage 中，后续访问无需重复输入

**关闭认证**（生产环境不推荐）：
```json
{
  "gateway": {
    "auth": {
      "mode": "none"
    }
  }
}
```

**修改 Token：**
1. 编辑 `~/.quantclaw/quantclaw.json`，修改 `gateway.auth.token` 的值
2. 运行 `quantclaw config reload`（或重启网关）
3. 清除浏览器中 `127.0.0.1:18801` 的 localStorage，然后输入新 token

## 命令行使用

```bash
# 发送消息（自动创建会话）
quantclaw agent "今天天气怎么样？"

# 使用指定会话
quantclaw agent --session my:project "继续上次的讨论"

# 一次性查询，不创建会话
quantclaw eval "2 + 2 等于多少？"

# 查看网关状态
quantclaw health
quantclaw status
```

## 配置

主配置文件为 `~/.quantclaw/quantclaw.json`：

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

详见[配置参考](/zh/guide/configuration)。

## 下一步

- 📖 [配置参考](/zh/guide/configuration)
- 🏗️ [架构说明](/zh/guide/architecture)
- 🔌 [插件开发](/zh/guide/plugins)
- 🛠️ [CLI 参考](/zh/guide/cli-reference)

## 故障排除

### 编译错误

```bash
# 检查系统依赖
sudo apt install build-essential cmake libssl-dev \
  libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev

# 查看详细错误
cd build
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON
make -j$(nproc)
```

### 网关无法启动

```bash
quantclaw config get gateway.port   # 检查端口配置
quantclaw doctor                    # 运行完整诊断
```

### API Key 问题

```bash
quantclaw health                    # 检查网关连通性
quantclaw config get llm.model      # 验证模型/Provider 配置
```

---

🎉 **已就绪！** 查看[特性指南](/zh/guide/features)了解更多能力。
