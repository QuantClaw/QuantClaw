// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "quantclaw/platform/process.hpp"

namespace quantclaw {

// Browser connection info
struct BrowserConnection {
  std::string cdp_url;       // Chrome DevTools Protocol WebSocket URL
  std::string pid_or_id;     // Browser PID or container ID
  bool is_remote = false;
  bool is_running = false;
};

// Browser page state
struct PageState {
  std::string url;
  std::string title;
  std::vector<std::string> console_messages;
  std::vector<std::string> errors;
  int request_count = 0;
};

// SSRF policy for navigation
struct SsrfPolicy {
  std::vector<std::string> blocked_hosts;   // e.g. ["localhost", "127.0.0.1"]
  std::vector<std::string> blocked_ranges;  // e.g. ["10.0.0.0/8", "192.168.0.0/16"]
  std::vector<std::string> allowed_hosts;   // If non-empty, only these allowed

  bool is_allowed(const std::string& host) const;

  static SsrfPolicy default_policy();
};

// Browser tool configuration
struct BrowserToolConfig {
  // How to get a browser
  enum class Mode {
    kLocal,    // Spawn local Chromium/Chrome
    kRemote,   // Connect to remote browser via CDP
  };

  Mode mode = Mode::kLocal;
  std::string chromium_path;      // Path to chromium binary (auto-detect if empty)
  std::string remote_cdp_url;     // CDP URL for remote mode
  bool headless = true;
  int viewport_width = 1280;
  int viewport_height = 720;
  int navigation_timeout_ms = 30000;
  SsrfPolicy ssrf_policy;

  static BrowserToolConfig FromJson(const nlohmann::json& j);
};

// Browser session: manages a browser instance and page interactions
class BrowserSession {
 public:
  explicit BrowserSession(std::shared_ptr<spdlog::logger> logger);
  ~BrowserSession();

  // Initialize browser (launch or connect)
  bool initialize(const BrowserToolConfig& config);

  // Close browser
  void close();

  // Navigation
  bool navigate(const std::string& url);
  std::string current_url() const;
  std::string page_title() const;

  // Page interaction (via CDP)
  std::string evaluate_js(const std::string& expression);
  bool click(const std::string& selector);
  bool type(const std::string& selector, const std::string& text);

  // Screenshots
  std::string screenshot_base64(bool full_page = false);

  // State
  PageState get_state() const;
  bool is_connected() const;

  // Get connection info
  const BrowserConnection& connection() const { return connection_; }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  BrowserToolConfig config_;
  BrowserConnection connection_;
  PageState state_;
  mutable std::mutex mu_;
  platform::ProcessId browser_pid_ = platform::kInvalidPid;

  // Launch local browser
  bool launch_local();
  // Connect to remote CDP
  bool connect_remote();

  // CDP communication
  std::string cdp_send(const std::string& method,
                        const nlohmann::json& params = {});

  // Find chromium binary
  static std::string find_chromium();

  // Check SSRF policy
  bool check_navigation(const std::string& url) const;
};

// Browser tool functions for registration with ToolRegistry
namespace browser_tools {

// Create browser tool schemas for LLM function calling
std::vector<nlohmann::json> get_tool_schemas();

// Create a browser tool executor
std::function<std::string(const nlohmann::json&)>
create_executor(std::shared_ptr<BrowserSession> session);

}  // namespace browser_tools

}  // namespace quantclaw
