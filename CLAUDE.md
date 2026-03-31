# QuantClaw C++23 Modules Fork

This is a fork of QuantClaw optimized for C++23 modules and modern C++ practices.

## Fork Status

**Current State:** C++23 modules migration complete ✅

### Key Changes from Upstream

1. **C++23 Standard**
   - Migrated from C++17 to C++23
   - All modules use `.cppm` interface files and `.cpp` implementation files
   - Standard library imported via `import std;`

2. **Module Architecture**
   - Global module fragments properly configured for non-modular headers
   - All C++ standard types qualified with `std::` (e.g., `std::uint16_t`, `std::int64_t`)
   - POSIX functions and macros preserved in C headers within global module fragments
   - nlohmann JSON upgraded to modular import: `import nlohmann.json;`

3. **Build System**
   - CMake 3.20+ required for C++23 modules support
   - GCC 16+ with `-fmodules-ts` flag recommended
   - CMakePresets.json configured with `gcc16-ninja` preset
   - vcpkg integration for dependency management

4. **Compiler Support**
   - **Primary:** GCC 16+ (fully supported with `-fmodules-ts`)
   - **Secondary:** Clang (partial support for C++23 modules in development)
   - **Windows:** MSVC 17+ (experimental C++23 modules support; WSL2 with GCC 16 recommended)

## Building This Fork

### Prerequisites
- GCC 16+ (mandatory for C++23 modules)
- CMake 3.20+
- Linux/WSL2 recommended

### Build Commands
```bash
# Using CMakePresets
mkdir build-cmake43 && cd build-cmake43
cmake .. --preset gcc16-ninja
cmake --build . -j$(nproc)

# Or traditional approach
cmake .. -DCMAKE_CXX_STANDARD=23
cmake --build . -j$(nproc)
```

## Module Guidelines for Contributors

### Files
- `.cppm` — Module interface units (replaces `.hpp`)
- `.cpp` — Module implementation units (requires `module;` and `module X;` declarations)
- Global module fragment in every `.cpp` for non-modular `#include` statements

### Standards
1. Use `import std;` for standard library — do NOT mix with `#include`
2. Qualify bare C types: `std::uint16_t`, `std::int64_t`, etc.
3. Third-party headers go in global module fragment (before `module` declaration)
4. POSIX functions require explicit C headers (e.g., `#include <cstdlib>` for `setenv()`)
5. Structured bindings with `nlohmann::json`: use iterator pattern (`it.key()`, `it.value()`) instead of `[k, v]` due to ADL visibility

### Known Workarounds
- **nlohmann JSON `.items()` with structured bindings:** Use explicit iterators
  ```cpp
  // Instead of: for (auto [k, v] : j.items()) { ... }
  for (auto it = j.begin(); it != j.end(); ++it) {
    auto k = it.key();
    auto v = it.value();
    // ...
  }
  ```

- **POSIX function visibility:** Always include C headers in global module fragment
  ```cpp
  module;
  #include <cstdlib>  // For setenv()
  #include <unistd.h> // For fork()
  module quantclaw.my.module;
  ```

## Migration Complete

All 10 critical issues from the C++23 modules review have been resolved:
1. ✅ Removed illegal module declarations from implementation files
2. ✅ Moved non-modular #includes to global module fragments
3. ✅ Replaced #include with import for modular third-party libraries
4. ✅ Qualified bare C types with std::
5. ✅ Fixed structured binding workarounds for nlohmann JSON
6. ✅ Cleaned CMakeLists.txt module sources
7. ✅ Verified build with zero errors in src/

## Testing

```bash
cd build-cmake43
cmake --build . --target test
./quantclaw_tests
```

## Documentation Updates

All documentation has been updated to reflect C++23 modules:
- `README.md` — Updated version badge and features
- `website/guide/building.md` — Complete C++23 modules build guide
- `website/guide/architecture.md` — Added modules context
- `website/guide/contributing.md` — Updated build instructions for modules
- `website/guide/getting-started.md` — GCC 16+ requirement
- `website/guide/features.md` — C++23 native performance
- `website/guide/installation.md` — Compiler requirements

## Future Improvements

- [ ] Full Clang C++23 modules support when stable
- [ ] MSVC C++23 modules support maturation
- [ ] Precompiled module caches for faster CI builds
- [ ] macOS GCC 16+ build verification

---

**Last Updated:** March 2026
**Fork Maintainer:** Joseph Mazzini (@jmazz)
