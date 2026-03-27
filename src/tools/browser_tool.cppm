// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.tools.browser_tool;

import std;
import nlohmann.json;
import <ixwebsocket/IXWebSocket.h>;
import quantclaw.platform.process;

namespace spdlog {
class logger;
}

export namespace quantclaw {

struct BrowserConnection {
  std::string cdp_url;
  std::string pid_or_id;
  bool is_remote = false;
  std::atomic<bool> is_running{false};

  BrowserConnection() = default;
  BrowserConnection(const BrowserConnection&) = delete;
  BrowserConnection& operator=(const BrowserConnection&) = delete;
};

struct PageState {
  std::string url;
  std::string title;
  std::vector<std::string> console_messages;
  std::vector<std::string> errors;
  int request_count = 0;
};

struct SsrfPolicy {
  std::vector<std::string> blocked_hosts;
  std::vector<std::string> blocked_ranges;
  std::vector<std::string> allowed_hosts;

  bool is_allowed(const std::string& host) const;
  static SsrfPolicy default_policy();
};

struct BrowserToolConfig {
  enum class Mode {
    kLocal,
    kRemote,
  };

  Mode mode = Mode::kLocal;
  std::string chromium_path;
  std::string remote_cdp_url;
  bool headless = true;
  int viewport_width = 1280;
  int viewport_height = 720;
  int navigation_timeout_ms = 30000;
  int cdp_debug_port = 9222;
  SsrfPolicy ssrf_policy;

  static BrowserToolConfig FromJson(const nlohmann::json& j);
};

class BrowserSession {
 public:
  explicit BrowserSession(std::shared_ptr<spdlog::logger> logger);
  ~BrowserSession();

  bool initialize(const BrowserToolConfig& config);
  void close();
  bool navigate(const std::string& url);
  std::string current_url() const;
  std::string page_title() const;
  std::string evaluate_js(const std::string& expression);
  bool click(const std::string& selector);
  bool type(const std::string& selector, const std::string& text);
  std::string screenshot_base64(bool full_page = false);
  PageState get_state() const;
  bool is_connected() const;

  const BrowserConnection& connection() const { return connection_; }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  BrowserToolConfig config_;
  BrowserConnection connection_;
  PageState state_;
  mutable std::mutex mu_;
  platform::ProcessId browser_pid_ = platform::kInvalidPid;
  ix::WebSocket cdp_ws_;
  std::atomic<int> cdp_id_{0};
  mutable std::mutex cdp_mu_;
  std::condition_variable cdp_cv_;
  std::unordered_map<int, nlohmann::json> cdp_responses_;
  std::set<int> cdp_pending_ids_;

  bool launch_local();
  bool connect_remote();
  bool connect_cdp_websocket(const std::string& ws_url);
  std::string cdp_send(const std::string& method,
                       const nlohmann::json& params = {});
  static std::string find_chromium();
  bool check_navigation(const std::string& url) const;
};

namespace browser_tools {

std::vector<nlohmann::json> get_tool_schemas();

std::function<std::string(const nlohmann::json&)>
create_executor(std::shared_ptr<BrowserSession> session);

}  // namespace browser_tools

}  // namespace quantclaw