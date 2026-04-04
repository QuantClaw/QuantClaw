# QuantClaw Gateway Windows Setup Script
# 配置 Windows 计划任务以启动 QuantClaw Gateway
# 参考 OpenClaw Windows 安装文档

param(
    [string]$TaskName = "QuantClaw-Gateway",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# 检查管理员权限
if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "此脚本需要管理员权限运行。请右键选择“以管理员身份运行”。"
    exit 1
}

Write-Host "QuantClaw Gateway Windows Setup" -ForegroundColor Cyan
Write-Host "=" * 50

# 查找 quantclaw.exe
$exePath = $null
$possiblePaths = @(
    "$env:USERPROFILE\.quantclaw\quantclaw.exe",
    "$env:USERPROFILE\AppData\Roaming\npm\quantclaw.exe",
    (Get-Location).Path + "\build\quantclaw.exe",
    (Get-Location).Path + "\build\Debug\quantclaw.exe",
    (Get-Location).Path + "\build\Release\quantclaw.exe"
)

foreach ($path in $possiblePaths) {
    if (Test-Path $path) {
        $exePath = $path
        Write-Host "✓ 找到 quantclaw: $exePath" -ForegroundColor Green
        break
    }
}

if (-not $exePath) {
    Write-Error "未找到 quantclaw.exe，请确保已编译或安装"
    Write-Host "`n可能的解决方案："
    Write-Host "1. 编译项目: cmake --build build"
    Write-Host "2. 全局安装: npm install -g quantclaw (如果支持)"
    exit 1
}

# 创建必要目录
$baseDir = Join-Path $env:USERPROFILE ".quantclaw"
$logsDir = Join-Path $baseDir "logs"
New-Item -ItemType Directory -Path $baseDir, $logsDir -Force | Out-Null

# 创建辅助启动脚本
$gatewayScript = Join-Path $baseDir "gateway.cmd"

@'
@echo off
chcp 65001 >nul
echo [%DATE% %TIME%] QuantClaw Gateway starting... >> "%~dp0logs\gateway-startup.log" 2>&1
cd /d "%~dp0"
"%QUANTCLAW_EXE%" gateway run >> "%~dp0logs\gateway.log" 2>&1
if %ERRORLEVEL% neq 0 (
    echo [%DATE% %TIME%] Gateway exited with code %ERRORLEVEL% >> "%~dp0logs\gateway-startup.log" 2>&1
)
'@ | Out-File -FilePath $gatewayScript -Encoding UTF8

# 更新 QUANTCLAW_EXE 环境变量
$scriptContent = Get-Content $gatewayScript -Raw
$scriptContent = $scriptContent -replace '%QUANTCLAW_EXE%', $exePath
Set-Content -Path $gatewayScript -Value $scriptContent -Encoding UTF8

Write-Host "✓ 创建启动脚本: $gatewayScript" -ForegroundColor Green

# 检查现有任务
$existingTask = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($existingTask) {
    if ($Force) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Host "✓ 已删除旧任务(Force)" -ForegroundColor Green
    } else {
        Write-Host "`n⚠ 计划任务已存在: $TaskName" -ForegroundColor Yellow
        $choice = Read-Host "是否删除并重新创建? (y/n)"
        if ($choice -ieq 'y') {
            Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
            Write-Host "✓ 已删除旧任务" -ForegroundColor Green
        } else {
            Write-Host "✗ 已取消操作" -ForegroundColor Red
            exit 0
        }
    }
}

# 创建计划任务
$action = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c `"$gatewayScript`""
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -DontStopIfGoingOnBatteries -AllowStartIfOnBatteries

Register-ScheduledTask -TaskName $TaskName `
    -Action $action `
    -Trigger $trigger `
    -Principal $principal `
    -Settings $settings `
    -Description "QuantClaw Gateway 自动启动服务"

Write-Host "`n✓ 计划任务创建成功: $TaskName" -ForegroundColor Green

# 创建配置文件（如果不存在）
$configPath = Join-Path $baseDir "quantclaw.json"
if (-not (Test-Path $configPath)) {
    Write-Host "`n📝 创建默认配置文件..." -ForegroundColor Cyan

    $defaultConfig = @{
        agent = @{
            model = "openai/qwen-max"
            maxIterations = 15
            temperature = 0.7
        }
        gateway = @{
            port = 18789
            bind = "loopback"
            auth = @{
                mode = "token"
                token = -join ((48..57) + (65..90) + (97..122) | Get-Random -Count 32 | ForEach-Object {[char]$_})
            }
            controlUi = @{
                enabled = $true
                port = 18790
            }
        }
        providers = @{}
        models = @{
            defaultModel = "openai/qwen-max"
            providers = @{}
        }
        channels = @{}
        tools = @{
            allow = @("group:fs", "group:runtime")
            deny = @()
        }
    } | ConvertTo-Json -Depth 10

    Set-Content -Path $configPath -Value $defaultConfig -Encoding UTF8
    Write-Host "✓ 创建配置文件: $configPath" -ForegroundColor Green
}

# 立即启动任务（可选）
Write-Host "`n❓ 是否立即启动 Gateway?" -ForegroundColor Cyan
$choice = Read-Host "(y/n, 默认 n)"
if ($choice -eq 'y') {
    Start-ScheduledTask -TaskName $TaskName
    Write-Host "`n✓ 已启动任务" -ForegroundColor Green
    Start-Sleep -Seconds 3

    # 检查是否运行
    try {
        $process = Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $exePath } | Select-Object -First 1
        if (-not $process) {
            $process = Get-Process -Name "quantclaw" -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -eq $exePath } | Select-Object -First 1
        }
    } catch {
        $process = Get-Process -Name "quantclaw" -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($process) {
        $pid = if ($process.ProcessId) { $process.ProcessId } else { $process.Id }
        Write-Host "✓ Gateway 进程正在运行 (PID: $pid)" -ForegroundColor Green
    } else {
        Write-Host "⚠ 未检测到 Gateway 进程，可能需要手动检查" -ForegroundColor Yellow
    }
}

Write-Host "`n" + "=" * 50 -ForegroundColor Cyan
Write-Host "设置完成！" -ForegroundColor Green
Write-Host "`n后续操作："
Write-Host "1. 编辑配置: notepad `"$configPath`""
Write-Host "2. 检查状态: schtasks /query /tn $TaskName"
Write-Host "3. 手动启动: schtasks /run /tn $TaskName"
Write-Host "4. 停止服务: schtasks /end /tn $TaskName"
Write-Host "5. 删除任务: schtasks /delete /tn $TaskName /f"
Write-Host "`n日志文件位置:"
Write-Host "   启动日志: $env:USERPROFILE\.quantclaw\logs\gateway-startup.log"
Write-Host "   运行日志: $env:USERPROFILE\.quantclaw\logs\gateway.log"
Write-Host "`nGateway URL: http://localhost:18790" -ForegroundColor Cyan
Write-Host ""
