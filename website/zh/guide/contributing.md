# 参与贡献

欢迎贡献！本指南介绍如何向 QuantClaw 提交代码。

## 工作流程

1. Fork 仓库并本地克隆
2. 创建功能分支：`git checkout -b feat/my-feature` 或 `fix/issue-123`
3. 编写代码，并为新功能添加测试
4. 格式化代码：`./scripts/format-code.sh`（或 Docker：`./scripts/format-code-docker.sh`）
5. 运行测试：`cd build && ctest --output-on-failure`
6. 提交并推送，然后向 `main` 分支发起 Pull Request

## 代码规范

QuantClaw 遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)，用 `clang-format` 强制执行。

### VS Code 配置

在 `.vscode/settings.json` 中添加：

```json
{
    "C_Cpp.clang_format_style": "file",
    "editor.formatOnSave": true
}
```

### Pre-commit Hook（每次提交前自动格式化）

```bash
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
./scripts/format-code.sh
git add -u
EOF
chmod +x .git/hooks/pre-commit
```

## 运行测试

```bash
cd build
./quantclaw_tests
# 或
ctest --output-on-failure

# 运行指定测试套件
./quantclaw_tests --gtest_filter=AgentLoopTest.*
```

## 编写测试

测试使用 [Google Test](https://github.com/google/googletest)。

```cpp
#include <gtest/gtest.h>
#include "quantclaw/my_module.hpp"

TEST(MyModuleTest, BasicFunctionality) {
    MyModule module;
    EXPECT_TRUE(module.initialize());
    EXPECT_EQ(module.getValue(), 42);
}
```

**注意事项：**
- 不同测试文件中不能有同名 fixture 类（会导致 ODR 违反）
- 需要 spdlog 时用 `null_sink_mt` 代替 `stdout_color_mt`
- 注意 try/catch 边界，避免重复 catch 块

## Commit 消息格式

| 前缀 | 用途 |
|------|------|
| `feat:` | 新功能 |
| `fix:` | Bug 修复 |
| `docs:` | 文档变更 |
| `refactor:` | 代码重构 |
| `test:` | 添加或更新测试 |
| `chore:` | 构建 / 工具链变更 |

**示例：**
```
feat: add web_search Tavily provider with fallback to DuckDuckGo
fix: correct session key normalization in SessionManager
docs: update CLI reference with correct gateway commands
```

## Pull Request 检查清单

- [ ] 所有测试通过（`ctest --output-on-failure`）
- [ ] 代码已用 `clang-format` 格式化（CI 会检查）
- [ ] 无新增编译器警告
- [ ] 如果新增了用户可见的功能，已更新 README
- [ ] 为新功能添加了单元测试

## 项目结构

```
QuantClaw/
├── src/                    # C++ 源码
│   ├── cli/                # CLI 命令
│   ├── core/               # Agent 核心
│   ├── gateway/            # RPC 网关
│   ├── providers/          # LLM Provider
│   ├── tools/              # 工具实现
│   └── ...
├── include/quantclaw/      # 公共头文件
├── tests/                  # C++ 测试（37 个文件）
├── sidecar/                # Node.js Sidecar
│   ├── src/                # TypeScript 源码（11 个文件）
│   └── test/               # Sidecar 测试
├── skills/                 # 内置技能
├── adapters/               # 频道适配器
├── scripts/                # 构建/发布脚本
├── website/                # 官网源码（VitePress）
└── docs/                   # 其他文档
```

## 常见问题

### 编译错误

- 确认 GCC 7+ 或 Clang 5+，需支持 C++17
- 安装所有系统依赖（见[安装说明](/zh/guide/installation)）

### 测试失败

- 优先看 `--output-on-failure` 的详细输出
- 检查是否有重复的 fixture 类名（ODR 违反）
- IPC 测试用 TCP loopback，无需 Unix socket

### 格式化检查失败（CI）

```bash
./scripts/format-code.sh
git add -u && git commit --amend
```

## 许可证

贡献的代码将以 Apache 2.0 协议发布，与项目保持一致。

---

有问题？欢迎提 [Issue](https://github.com/QuantClaw/QuantClaw/issues) 或发起 [Discussion](https://github.com/QuantClaw/QuantClaw/discussions)。
