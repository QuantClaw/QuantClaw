# Contributing to QuantClaw

Thank you for your interest in contributing to QuantClaw! This guide will help you get started.

## Code of Conduct

Please note that this project is released with a [Contributor Code of Conduct](https://github.com/QuantClaw/quantclaw/blob/main/CODE_OF_CONDUCT.md). By participating, you agree to abide by its terms.

## Ways to Contribute

### 1. Report Bugs

Found a bug? Please open an issue on [GitHub](https://github.com/QuantClaw/quantclaw/issues).

**Include:**
- Clear description of the problem
- Steps to reproduce
- Expected vs actual behavior
- Environment (OS, version, etc.)

### 2. Request Features

Have an idea for improvement? Open a [feature request](https://github.com/QuantClaw/quantclaw/discussions).

**Describe:**
- The problem you're trying to solve
- How the feature would work
- Any alternatives considered

### 3. Submit Code

Ready to code? Follow the workflow below.

### 4. Improve Documentation

Documentation improvements are always welcome:
- Fix typos
- Clarify explanations
- Add examples
- Update outdated information

### 5. Help Others

- Answer questions in [Discussions](https://github.com/QuantClaw/quantclaw/discussions)
- Review pull requests
- Share your use cases

## Development Setup

### Fork and Clone

```bash
# Fork the repository on GitHub
# Then clone your fork
git clone https://github.com/YOUR_USERNAME/quantclaw.git
cd quantclaw

# Add upstream
git remote add upstream https://github.com/QuantClaw/quantclaw.git
```

### Build and Test

```bash
# Install dependencies (see Building Guide)
mkdir build && cd build
cmake ..
cmake --build . --parallel

# Run tests
./quantclaw_tests

# Test locally
quantclaw --version
```

## Making Changes

### Create a Branch

```bash
# Get latest main
git fetch upstream
git checkout main
git pull upstream main

# Create feature branch
git checkout -b feature/my-feature
```

### Follow Code Style

**C++ Style Guide**: Google C++ Style Guide (enforced by clang-format)

```bash
# Format your code
clang-format -i src/my_file.cpp
clang-format -i include/quantclaw/my_file.hpp

# Check style
clang-tidy src/my_file.cpp
```

**File Naming:**
- Source files: `snake_case.cpp`
- Headers: `snake_case.hpp`
- Classes: `PascalCase`
- Functions: `snake_case`
- Constants: `SCREAMING_SNAKE_CASE`

### Write Tests

Add tests for your changes:

```cpp
// tests/test_my_feature.cpp
#include <gtest/gtest.h>
#include "quantclaw/my_feature.hpp"

class MyFeatureTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Setup code
  }

  void TearDown() override {
    // Cleanup code
  }
};

TEST_F(MyFeatureTest, BasicFunctionality) {
  MyFeature feature;
  EXPECT_EQ(feature.doSomething(), expected_result);
}

TEST_F(MyFeatureTest, EdgeCase) {
  // Test edge cases
}
```

Run tests:

```bash
cd build
cmake --build . && ./quantclaw_tests
ctest -V
```

### Commit Messages

Write clear, descriptive commit messages:

```
Short summary (50 chars max)

More detailed explanation if needed.
Explain the "why" not just the "what".

Fixes #123  # Reference issues
```

**Examples:**
```
fix: Handle null pointer in context manager
feat: Add BM25 memory search
docs: Clarify configuration options
test: Add tests for failover logic
refactor: Extract tool registry logic
```

### Push and Create PR

```bash
# Push your branch
git push origin feature/my-feature

# Create pull request on GitHub
# Link related issues
# Describe your changes
# Request reviewers
```

## Pull Request Guidelines

**Before submitting:**
- [ ] Code follows style guide (clang-format)
- [ ] Tests added/updated
- [ ] Documentation updated
- [ ] No breaking changes (or documented)
- [ ] Builds without errors
- [ ] All tests pass

**PR Description:**
- Clear title
- What changed and why
- Related issues/discussions
- Screenshots if UI changes

**Example PR:**
```
Title: Add context overflow detection

## Changes
- Implemented token counting for context
- Auto-detect when context exceeds limits
- Graceful fallback to compaction

## Testing
- Added 5 new tests
- Verified with large contexts (100k tokens)

## Related
Fixes #456
Related to #123
```

## Review Process

1. **Automated Checks**: Tests, linting, coverage
2. **Code Review**: Maintainers review for:
   - Code quality
   - Performance
   - Security
   - API design
3. **Feedback**: Address review comments
4. **Approval**: Maintainers approve
5. **Merge**: Changes integrated

## Common Issues

### Build Fails

```bash
# Update dependencies
git submodule update --init --recursive

# Clean and rebuild
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
```

### Tests Fail

```bash
# Run specific test
./quantclaw_tests --gtest_filter="MyTest*"

# Run with verbose output
./quantclaw_tests -v

# Check test logs
tail -f /var/log/quantclaw.log
```

### Style Check Fails

```bash
# Format your code
clang-format -i src/my_file.cpp

# Check formatting
clang-format --dry-run src/my_file.cpp
```

## Documentation

### Update Documentation

Website documentation is in `website/guide/`:

```bash
# Edit markdown files
vim website/guide/getting-started.md

# Build website locally
cd website
npm install
npm run docs:dev
# Visit http://localhost:5173
```

### Add Code Comments

Use clear, concise comments:

```cpp
// Good: Explains the why
// Use exponential backoff to avoid overwhelming the provider
int delay_ms = base_delay * pow(2, retry_count);

// Bad: States the obvious
// Multiply delay by 2
int delay_ms = base_delay * 2;
```

## Plugin Development

Contributing a plugin or skill:

1. **Create plugin** (see [Plugin Guide](/guide/plugins))
2. **Add tests** for your plugin
3. **Document** usage and configuration
4. **Submit PR** to add to ecosystem registry

**Plugin template:**
```bash
quantclaw skill create my-plugin
cd my-plugin
# Implement and test
npm test
```

## Release Process

Releases follow [Semantic Versioning](https://semver.org/):

- `MAJOR`: Breaking changes
- `MINOR`: New features
- `PATCH`: Bug fixes

**For maintainers:**
```bash
# Update version
# Update CHANGELOG
# Tag release
git tag v1.0.0
git push origin v1.0.0
```

## Acknowledgments

Contributors will be recognized in:
- GitHub contributors page
- CONTRIBUTORS.md file
- Release notes

## License

By contributing, you agree that your contributions will be licensed under the [MIT License](https://github.com/QuantClaw/quantclaw/blob/main/LICENSE).

## Questions?

- **GitHub Issues**: [Ask a question](https://github.com/QuantClaw/quantclaw/issues/new?labels=question)
- **Discussions**: [Community forum](https://github.com/QuantClaw/quantclaw/discussions)
- **Email**: contact@quantclaw.io (if applicable)

---

**Thank you for helping make QuantClaw better! 🎉**
