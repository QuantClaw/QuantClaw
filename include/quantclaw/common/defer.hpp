// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <type_traits>
#include <utility>

// ─── Defer / MakeDefer / DEFER ─────────────────────────────────────────────
//
// Executes a callable when the enclosing scope exits — Go-style deferred
// cleanup, but with explicit dismiss() to cancel on the success path.
//
// Usage (factory function):
//   auto guard = MakeDefer([&] { fclose(fp); });
//   // ... risky work ...
//   guard.dismiss();   // success: cancel cleanup
//
// Usage (inline macro — unique variable per line):
//   DEFER(fclose(fp));
//   DEFER({ unlock(mu); log("done"); });
//
// Properties:
//   • Move-constructible (not copyable)
//   • arm() re-enables after dismiss()
//   • is_active() queries current state
//   • noexcept destructor — callable must not throw

namespace quantclaw {

template <typename F>
class Defer {
 public:
  explicit Defer(F action) noexcept(std::is_nothrow_move_constructible_v<F>)
      : action_(std::move(action)), active_(true) {}

  Defer(Defer&& other) noexcept(std::is_nothrow_move_constructible_v<F>)
      : action_(std::move(other.action_)), active_(other.active_) {
    other.active_ = false;
  }

  ~Defer() noexcept {
    if (active_)
      action_();
  }

  Defer(const Defer&) = delete;
  Defer& operator=(const Defer&) = delete;
  Defer& operator=(Defer&&) = delete;

  // Cancel the deferred action (idempotent).
  void dismiss() noexcept {
    active_ = false;
  }

  // Re-arm after dismiss() (e.g., for retry loops).
  void arm() noexcept {
    active_ = true;
  }

  bool is_active() const noexcept {
    return active_;
  }

 private:
  F action_;
  bool active_;
};

template <typename F>
[[nodiscard]] Defer<std::decay_t<F>> MakeDefer(F&& f) {
  return Defer<std::decay_t<F>>(std::forward<F>(f));
}

}  // namespace quantclaw

// DEFER(code) — statement-level defer; code may be a single expression or a
// braced block.  The variable name embeds __LINE__ to avoid shadowing.
// Two-level macro expansion is required to stringify __LINE__ correctly.
#define QC_DEFER_CONCAT_IMPL(a, b) a##b
#define QC_DEFER_CONCAT(a, b) QC_DEFER_CONCAT_IMPL(a, b)
#define DEFER(code)                               \
  auto QC_DEFER_CONCAT(_defer_guard_, __LINE__) = \
      ::quantclaw::MakeDefer([&]() noexcept { code; })
