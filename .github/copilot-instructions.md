# GitHub Copilot Instructions for QuantClaw

## Project Overview

QuantClaw is a high-performance C++17 agent gateway server — a reimplementation of
[OpenClaw](https://github.com/OpenClaw) with the same CLI and session/plugin
ecosystem compatibility.

## Language & Standard

- **C++17** throughout (no C++20 features)
- Google C++ Style Guide (enforced by `.clang-tidy` and `clang-format-14`)
- All new public APIs must live under the `quantclaw::` namespace

## Architecture Highlights

- `src/core/agent_loop.cpp` — main LLM turn loop; holds `shared_ptr` to providers, memory, tools
- `src/gateway/` — WebSocket RPC gateway (IXWebSocket), command queue, RPC handlers
- `src/session/session_manager.cpp` — JSONL transcript storage with `shared_mutex` read/write split
- `src/providers/` — multi-provider LLM abstraction with exponential-backoff failover
- `sidecar/` — Node.js sidecar process (TypeScript) for plugin/skill execution over TCP IPC
- `include/quantclaw/common/` — shared utilities: `try.hpp`, `defer.hpp`, `string_util.hpp`,
  `parse_util.hpp`, `noncopyable.hpp`

## Review Priorities

When reviewing pull requests, focus on:

1. **Thread safety** — `SessionManager`, `CommandQueue`, and `GatewayServer` are shared across
   threads. Read paths must use `shared_lock<shared_mutex>`; write paths must use `unique_lock`.

2. **Memory safety** — prefer `shared_ptr`/`unique_ptr` over raw pointers. Flag any use of
   `new`/`delete` outside of factory functions.

3. **Error handling** — functions returning `std::optional<T>` must never throw; use
   `QC_TRY(expr)` for propagating failures in call chains that return optional/expected.

4. **Security** — no `getenv()` calls inside library code (only in `main.cpp` and CLI handlers);
   validate all external input (WebSocket messages, file paths) before use.

5. **OpenClaw compatibility** — CLI subcommands, RPC method names, and session key format
   (`agent:<agentId>:<rest>`) must remain identical to OpenClaw.

6. **Sanitizer cleanliness** — code must pass ASAN, TSAN, and UBSAN (see CI `sanitizers` job).

## What to Ignore

- `ui/` — vendored Lit Web Components frontend; do not review
- `refs/` — reference source trees used for inspiration only; do not review
- `*.md` — documentation files
- Generated files under `build/`

## Code Patterns in Use

```cpp
// Preferred: RAII scope guard
DEFER(cleanup());

// Preferred: error propagation without exceptions
auto val = QC_TRY(parse_something(input));

// Preferred: non-copyable service classes
class MyService : public Noncopyable { ... };

// Preferred: parse utilities (no exceptions, no implicit trim)
auto port = ParsePort("8080");  // returns std::optional<uint16_t>
```

## Test Coverage

All new functionality must be accompanied by GTest tests in `tests/`. Mock objects live in the
same test file. Use `spdlog::sinks::null_sink_mt` (not `stdout_color_mt`) in tests to suppress
log output.
