// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/signal_handler.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <thread>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace quantclaw {

std::atomic<bool> SignalHandler::shutdown_requested_{false};
SignalHandler::ShutdownCallback SignalHandler::shutdown_callback_;
SignalHandler::ReloadCallback SignalHandler::reload_callback_;

void SignalHandler::Install(ShutdownCallback on_shutdown,
                            ReloadCallback on_reload) {
  shutdown_callback_ = std::move(on_shutdown);
  reload_callback_ = std::move(on_reload);
  shutdown_requested_ = false;

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
#ifndef _WIN32
  std::signal(SIGUSR1, signal_handler);
  std::signal(SIGHUP, SIG_IGN);
#endif
}

void SignalHandler::WaitForShutdown() {
  while (!shutdown_requested_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool SignalHandler::ShouldShutdown() {
  return shutdown_requested_;
}

void SignalHandler::signal_handler(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
    // Prevent re-entrant shutdown: if already requested, force-exit.
    // Use std::_Exit() which is async-signal-safe and portable.
    if (shutdown_requested_.exchange(true)) {
      std::_Exit(128 + signum);
      return;
    }
    if (shutdown_callback_) {
      shutdown_callback_();
    }
  }
#ifndef _WIN32
  else if (signum == SIGUSR1) {
    if (reload_callback_) {
      reload_callback_();
    }
  }
#endif
}

}  // namespace quantclaw
