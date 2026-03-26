// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.core.signal_handler;

import std;

export namespace quantclaw {

class SignalHandler {
 public:
  using ShutdownCallback = std::function<void()>;
  using ReloadCallback = std::function<void()>;

  static void Install(ShutdownCallback on_shutdown,
                      ReloadCallback on_reload = nullptr);
  static void WaitForShutdown();
  static bool ShouldShutdown();

 private:
  static std::atomic<bool> shutdown_requested_;
  static ShutdownCallback shutdown_callback_;
  static ReloadCallback reload_callback_;
  static void signal_handler(int signum);
};

}  // namespace quantclaw