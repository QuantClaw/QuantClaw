// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.common.defer;

import std;

export namespace quantclaw {

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
  bool active_ = false;
};

template <typename F>
[[nodiscard]] Defer<std::decay_t<F>> MakeDefer(F&& f) {
  return Defer<std::decay_t<F>>(std::forward<F>(f));
}

}  // namespace quantclaw