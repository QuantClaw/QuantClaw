# QuantClaw - Windows 测试权限问题解决方案

## 问题描述

根据 OpenClaw Windows 安装文档，Windows 笔记本环境下，QuantClaw Gateway 服务启动时会遇到权限问题。原因是 `gateway install` 命令尝试创建计划任务（schtasks），但缺少管理员权限。

## 解决方案

### 方案一：使用计划任务安装脚本（推荐）

1. **以管理员身份运行 PowerShell**

   右键点击 PowerShell → "以管理员身份运行"

2. **执行安装脚本**

   ```powershell
   cd d:\test\github\QuantClaw
   powershell -ExecutionPolicy Bypass -File scripts\gateway-setup-windows.ps1
   ```

3. **脚本功能**
   - ✅ 自动查找 quantclaw.exe
   - ✅ 创建计划任务（开机自动启动）
   - ✅ 生成配置文件（如果不存在）
   - ✅ 创建启动辅助脚本 (`~\.quantclaw\gateway.cmd`)
   - ✅ 生成日志文件

4. **验证安装**

   ```powershell
   # 检查任务状态
   schtasks /query /tn "QuantClaw-Gateway"

   # 检查端口监听（18789 是默认 gateway 端口）
   netstat -ano | findstr :18789

   # 手动启动任务
   schtasks /run /tn "QuantClaw-Gateway"

   # 停止任务
   schtasks /end /tn "QuantClaw-Gateway"

   # 删除任务
   schtasks /delete /tn "QuantClaw-Gateway" /f
   ```

5. **访问控制界面**

   打开浏览器访问: `http://localhost:18790`

---

### 方案二：手动启动（无需管理员权限）

1. **使用手动启动脚本**

   ```batch
   cd d:\test\github\QuantClaw
   scripts\gateway-manual.bat
   ```

2. **或直接命令行启动**

   ```batch
   # 在项目目录下
   build\quantclaw.exe gateway run

   # 或全局安装后
   quantclaw gateway run
   ```

3. **优点**
   - ✅ 无需管理员权限
   - ✅ 实时查看日志输出
   - ✅ 适合开发测试

4. **缺点**
   - ❌ 不会开机自启
   - ❌ 关闭终端即停止

---

### 方案三：修改当前代码实现（开发级方案）

#### 当前实现问题

Windows 的 [service_win32.cpp](file://d:\test\github\QuantClaw\src\platform\service_win32.cpp) 当前仅使用后台进程，没有利用 Windows 计划任务：

```cpp
// src/platform/service_win32.cpp
int ServiceManager::start() {
  std::string exe = executable_path();
  std::vector<std::string> args = {exe, "gateway", "run"};
  ProcessId pid = spawn_process(args);  // 仅启动后台进程
  // ...
}
```

#### 改进建议

可以添加对计划任务的支持，参考 OpenClaw 的做法：

```cpp
// 改进版本（伪代码）
int ServiceManager::install(int port) {
  // 1. 创建 gateway.cmd 启动脚本
  std::ofstream cmd_file(state_dir_ + "\\gateway.cmd");
  cmd_file << "@echo off\n";
  cmd_file << "chcp 65001 >nul\n";
  cmd_file << "\"" << exe << "\" gateway run --port " << port << "\n";

  // 2. 创建计划任务
  std::string schtasks_cmd =
    "schtasks /create /tn \"QuantClaw-Gateway\" "
    "/tr \"cmd.exe /c \\\"" + state_dir_ + "\\\\gateway.cmd\\\"\" "
    "/sc onlogon /ru \"%USERNAME%\" /rl highest /f";

  int ret = std::system(schtasks_cmd.c_str());
  if (ret != 0) {
    logger_->warn("计划任务创建失败，需要管理员权限");
    logger_->info("请以管理员身份运行: quantclaw gateway install");
    return 1;
  }

  logger_->info("计划任务创建成功");
  return 0;
}
```

---

## 测试建议

### 1. 单元测试验证

运行现有测试：

```bash
# Linux/Mac (在 WSL 中)
bash tests/test-cli.sh build/quantclaw
bash tests/test-onboard.sh build/quantclaw

# Windows PowerShell
# 需要先安装 curl, python3 等依赖
```

### 2. 手动集成测试

```powershell
# 1. 首次初始化
quantclaw onboard --quick

# 2. 启动 Gateway（方案二）
scripts\gateway-manual.bat

# 3. 新开终端，测试 CLI 命令
quantclaw status
quantclaw health
quantclaw config get
quantclaw skills list

# 4. 测试 agent
quantclaw agent -m "你好，测试一下"

# 5. 检查日志
type $env:USERPROFILE\.quantclaw\logs\gateway.log
```

### 3. 计划任务测试（方案一）

```powershell
# 1. 安装任务
powershell -ExecutionPolicy Bypass -File scripts\gateway-setup-windows.ps1

# 2. 启动任务
schtasks /run /tn "QuantClaw-Gateway"

# 3. 等待 5 秒后检查
Start-Sleep -Seconds 5
netstat -ano | findstr :18789

# 4. 测试连接
curl http://localhost:18790/api/health

# 5. 停止任务
schtasks /end /tn "QuantClaw-Gateway"
```

---

## 日志文件位置

```
%USERPROFILE%\.quantclaw\logs\
├── gateway.log              # Gateway 运行日志
├── gateway-startup.log      # 启动脚本日志
└── gateway-manual.log       # 手动启动日志
```

---

## 常见问题

### Q1: 运行脚本报错 "Execution Policy"

**解决**: 以管理员身份运行以下命令：

```powershell
Set-ExecutionPolicy Bypass -Scope Process
```

或在运行脚本时添加参数：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\gateway-setup-windows.ps1
```

### Q2: `schtasks` 命令失败

**原因**: 缺少管理员权限

**解决**:
- 右键 PowerShell → "以管理员身份运行"
- 或使用方案二（手动启动）

### Q3: 端口 18789 已被占用

**解决**: 修改配置文件

```json
// %USERPROFILE%\.quantclaw\quantclaw.json
{
  "gateway": {
    "port": 18800,  // 改为其他端口
    ...
  }
}
```

### Q4: 杀毒软件拦截

**解决**:
- 将 `quantclaw.exe` 添加到杀毒软件白名单
- 临时禁用杀毒软件（仅测试环境）

---

## 相关文件

- **安装脚本**: [scripts/gateway-setup-windows.ps1](scripts/gateway-setup-windows.ps1)
- **手动启动**: [scripts/gateway-manual.bat](scripts/gateway-manual.bat)
- **Windows 服务实现**: [src/platform/service_win32.cpp](src/platform/service_win32.cpp)
- **Gateway 命令**: [src/cli/gateway_commands.cpp](src/cli/gateway_commands.cpp)

---

## 参考

- OpenClaw Windows 安装文档: `refs/windows-openclaw.pdf`
- Linux 服务实现: [src/platform/service_unix.cpp](src/platform/service_unix.cpp) (使用 systemd)
- QuantClaw CLI 参考: [website/guide/cli-reference.md](website/guide/cli-reference.md)
