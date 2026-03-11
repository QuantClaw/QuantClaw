// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/tools/browser_tool.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>

#include <httplib.h>

#include "quantclaw/common/parse_util.hpp"

namespace quantclaw {

// --- SsrfPolicy ---

bool SsrfPolicy::is_allowed(const std::string& host) const {
  for (const auto& blocked : blocked_hosts) {
    if (host == blocked)
      return false;
  }

  for (const auto& range : blocked_ranges) {
    if (range == "10.0.0.0/8" && host.substr(0, 3) == "10.")
      return false;
    if (range == "172.16.0.0/12" && host.substr(0, 4) == "172.") {
      return false;
    }
    if (range == "192.168.0.0/16" && host.substr(0, 8) == "192.168.") {
      return false;
    }
  }

  if (!allowed_hosts.empty()) {
    for (const auto& allowed : allowed_hosts) {
      if (host == allowed)
        return true;
    }
    return false;
  }

  return true;
}

SsrfPolicy SsrfPolicy::default_policy() {
  SsrfPolicy p;
  p.blocked_hosts = {"localhost", "127.0.0.1", "0.0.0.0", "[::1]"};
  p.blocked_ranges = {"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"};
  return p;
}

// --- BrowserToolConfig ---

BrowserToolConfig BrowserToolConfig::FromJson(const nlohmann::json& j) {
  BrowserToolConfig c;
  if (j.contains("mode") && j["mode"].is_string()) {
    c.mode = (j["mode"] == "remote") ? BrowserToolConfig::Mode::kRemote
                                     : BrowserToolConfig::Mode::kLocal;
  }
  c.chromium_path = j.value("chromiumPath", std::string{});
  c.remote_cdp_url = j.value("remoteCdpUrl", std::string{});
  if (c.remote_cdp_url.empty()) {
    c.remote_cdp_url = j.value("cdpUrl", std::string{});
  }
  c.headless = j.value("headless", true);
  c.viewport_width = j.value("viewportWidth", 1280);
  c.viewport_height = j.value("viewportHeight", 720);
  c.navigation_timeout_ms = j.value("navigationTimeoutMs", 30000);
  c.cdp_debug_port = j.value("cdpDebugPort", 9222);

  if (j.contains("ssrf") && j["ssrf"].is_object()) {
    auto& ssrf = j["ssrf"];
    if (ssrf.contains("blockedHosts") && ssrf["blockedHosts"].is_array()) {
      for (const auto& h : ssrf["blockedHosts"]) {
        if (h.is_string())
          c.ssrf_policy.blocked_hosts.push_back(h);
      }
    }
    if (ssrf.contains("allowedHosts") && ssrf["allowedHosts"].is_array()) {
      for (const auto& h : ssrf["allowedHosts"]) {
        if (h.is_string())
          c.ssrf_policy.allowed_hosts.push_back(h);
      }
    }
  } else {
    c.ssrf_policy = SsrfPolicy::default_policy();
  }

  return c;
}

// --- BrowserSession ---

BrowserSession::BrowserSession(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

BrowserSession::~BrowserSession() {
  close();
}

bool BrowserSession::initialize(const BrowserToolConfig& config) {
  config_ = config;
  if (config_.mode == BrowserToolConfig::Mode::kRemote) {
    return connect_remote();
  }
  return launch_local();
}

void BrowserSession::close() {
  cdp_ws_.stop();
  if (browser_pid_ != platform::kInvalidPid) {
    platform::terminate_process(browser_pid_);
    platform::wait_process(browser_pid_, 0);  // non-blocking reap
    browser_pid_ = platform::kInvalidPid;
    logger_->info("Browser process terminated");
  }
  connection_.is_running = false;
}

bool BrowserSession::navigate(const std::string& url) {
  if (!check_navigation(url)) {
    logger_->warn("Navigation blocked by SSRF policy: {}", url);
    return false;
  }

  auto result = cdp_send("Page.navigate", {{"url", url}});
  if (result.empty())
    return false;

  std::lock_guard<std::mutex> lock(mu_);
  state_.url = url;
  logger_->info("Navigated to: {}", url);
  return true;
}

std::string BrowserSession::current_url() const {
  std::lock_guard<std::mutex> lock(mu_);
  return state_.url;
}

std::string BrowserSession::page_title() const {
  std::lock_guard<std::mutex> lock(mu_);
  return state_.title;
}

std::string BrowserSession::evaluate_js(const std::string& expression) {
  return cdp_send("Runtime.evaluate", {{"expression", expression}});
}

bool BrowserSession::click(const std::string& selector) {
  std::string js = "document.querySelector('" + selector + "')?.click()";
  auto result = evaluate_js(js);
  return !result.empty();
}

bool BrowserSession::type(const std::string& selector,
                          const std::string& text) {
  std::string js = "(() => { var el = document.querySelector('" + selector +
                   "'); if(el) { el.value = '" + text +
                   "'; el.dispatchEvent(new Event('input')); return true; } "
                   "return false; })()";
  auto result = evaluate_js(js);
  return result.find("true") != std::string::npos;
}

std::string BrowserSession::screenshot_base64(bool full_page) {
  nlohmann::json params;
  params["format"] = "png";
  if (full_page) {
    params["captureBeyondViewport"] = true;
  }
  return cdp_send("Page.captureScreenshot", params);
}

PageState BrowserSession::get_state() const {
  std::lock_guard<std::mutex> lock(mu_);
  return state_;
}

bool BrowserSession::is_connected() const {
  return connection_.is_running;
}

bool BrowserSession::launch_local() {
  std::string chromium = config_.chromium_path;
  if (chromium.empty()) {
    chromium = find_chromium();
  }
  if (chromium.empty()) {
    logger_->error("No Chromium/Chrome binary found");
    return false;
  }

  int port = config_.cdp_debug_port;
  std::vector<std::string> args = {
      chromium,
      "--remote-debugging-port=" + std::to_string(port),
      "--no-first-run",
      "--no-default-browser-check",
      "--disable-background-networking",
      "--disable-sync",
  };
  if (config_.headless) {
    args.push_back("--headless=new");
  }
  args.push_back("--window-size=" + std::to_string(config_.viewport_width) +
                 "," + std::to_string(config_.viewport_height));

  auto pid = platform::spawn_process(args);
  if (pid == platform::kInvalidPid) {
    logger_->error("Failed to launch browser process");
    return false;
  }

  browser_pid_ = pid;
  connection_.pid_or_id = std::to_string(pid);
  connection_.is_remote = false;
  logger_->info("Launched browser: PID={}, binary={}, port={}", pid, chromium,
                port);

  // Wait for DevTools HTTP endpoint to become ready (up to 5 seconds)
  std::string ws_url;
  for (int attempt = 0; attempt < 25; ++attempt) {
    try {
      httplib::Client http("127.0.0.1", port);
      http.set_connection_timeout(1, 0);
      auto res = http.Get("/json/version");
      if (res && res->status == 200) {
        auto j = nlohmann::json::parse(res->body);
        ws_url = j.value("webSocketDebuggerUrl", "");
        if (!ws_url.empty())
          break;
      }
    } catch (...) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  if (ws_url.empty()) {
    logger_->error("Browser DevTools endpoint not ready on port {}", port);
    close();
    return false;
  }

  logger_->info("Browser DevTools ready: {}", ws_url);
  bool connected = connect_cdp_websocket(ws_url);
  if (!connected) {
    logger_->warn(
        "Port {} may already be in use. Consider setting a unique "
        "cdp_debug_port.",
        port);
    close();
  }
  return connected;
}

bool BrowserSession::connect_remote() {
  if (config_.remote_cdp_url.empty()) {
    logger_->error("No remote CDP URL configured");
    return false;
  }

  // If given an HTTP URL, fetch /json/version to get the WS URL
  std::string ws_url = config_.remote_cdp_url;
  if (ws_url.rfind("http", 0) == 0) {
    try {
      // Parse host/port from HTTP URL
      bool is_https = (ws_url.rfind("https", 0) == 0);
      std::regex http_re(R"(https?://([^:/]+):?(\d*))");
      std::smatch m;
      if (std::regex_search(ws_url, m, http_re)) {
        std::string host = m[1].str();
        int port = 80;
        if (m[2].str().empty()) {
          port = is_https ? 443 : 80;
        } else {
          auto parsed = ParsePort(m[2].str());
          if (!parsed) {
            logger_->error("Invalid port in remote CDP URL: {}", ws_url);
            return false;
          }
          port = *parsed;
        }
        auto fetch_ws = [&](auto& client) {
          client.set_connection_timeout(3, 0);
          auto res = client.Get("/json/version");
          if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body);
            ws_url = j.value("webSocketDebuggerUrl", ws_url);
          }
        };
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (is_https) {
          httplib::SSLClient ssl_client(host, port);
          fetch_ws(ssl_client);
        } else {
          httplib::Client plain_client(host, port);
          fetch_ws(plain_client);
        }
#else
        httplib::Client plain_client(host, port);
        fetch_ws(plain_client);
#endif
      }
    } catch (...) {}
    // If the URL still starts with "http", we failed to resolve a WS URL
    if (ws_url.rfind("http", 0) == 0) {
      logger_->error("Failed to resolve WebSocket URL from HTTP endpoint: {}",
                     ws_url);
      return false;
    }
  }

  connection_.is_remote = true;
  logger_->info("Connecting to remote browser: {}", ws_url);
  return connect_cdp_websocket(ws_url);
}

bool BrowserSession::connect_cdp_websocket(const std::string& ws_url) {
  cdp_ws_.setUrl(ws_url);
  cdp_ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      cdp_cv_.notify_all();
    } else if (msg->type == ix::WebSocketMessageType::Message) {
      try {
        auto j = nlohmann::json::parse(msg->str);
        int msg_id = j.value("id", -1);
        if (msg_id >= 0) {
          {
            std::lock_guard<std::mutex> lock(cdp_mu_);
            // Only store response for requests still in-flight
            if (cdp_pending_ids_.count(msg_id)) {
              cdp_responses_[msg_id] = j;
            }
          }
          cdp_cv_.notify_all();
        }
      } catch (...) {}
    } else if (msg->type == ix::WebSocketMessageType::Error) {
      logger_->warn("CDP WebSocket error: {}", msg->errorInfo.reason);
    }
  });

  cdp_ws_.start();

  // Wait up to 3 seconds for the WebSocket to open
  {
    std::unique_lock<std::mutex> lock(cdp_mu_);
    bool opened = cdp_cv_.wait_for(lock, std::chrono::seconds(3), [this] {
      return cdp_ws_.getReadyState() == ix::ReadyState::Open;
    });
    if (opened) {
      connection_.cdp_url = ws_url;
      connection_.is_running = true;
      logger_->info("CDP WebSocket connected: {}", ws_url);
      return true;
    }
  }

  logger_->error("Failed to connect CDP WebSocket to: {}", ws_url);
  cdp_ws_.stop();
  return false;
}

std::string BrowserSession::cdp_send(const std::string& method,
                                     const nlohmann::json& params) {
  if (!connection_.is_running ||
      cdp_ws_.getReadyState() != ix::ReadyState::Open) {
    logger_->warn("CDP not connected, skipping: {}", method);
    return "{}";
  }

  int id = ++cdp_id_;
  nlohmann::json msg = {{"id", id}, {"method", method}, {"params", params}};

  // Register as pending before sending so the callback can match it
  {
    std::lock_guard<std::mutex> pending_lock(cdp_mu_);
    cdp_pending_ids_.insert(id);
  }
  cdp_ws_.send(msg.dump());
  logger_->debug("CDP send id={} method={}", id, method);

  // Wait for response
  std::unique_lock<std::mutex> lock(cdp_mu_);
  bool ok = cdp_cv_.wait_for(
      lock, std::chrono::milliseconds(config_.navigation_timeout_ms),
      [this, id] { return cdp_responses_.count(id) > 0; });

  if (!ok) {
    logger_->warn("CDP timeout for method: {}", method);
    cdp_responses_.erase(id);
    cdp_pending_ids_.erase(id);
    return "{}";
  }

  auto resp = cdp_responses_[id];
  cdp_responses_.erase(id);
  cdp_pending_ids_.erase(id);

  if (resp.contains("error")) {
    logger_->warn("CDP error for {}: {}", method, resp["error"].dump());
    return "{}";
  }

  return resp.value("result", nlohmann::json::object()).dump();
}

std::string BrowserSession::find_chromium() {
#ifdef _WIN32
  // Check common Windows Chrome paths
  std::vector<std::string> candidates;
  const char* pf = std::getenv("PROGRAMFILES");
  const char* pf86 = std::getenv("PROGRAMFILES(X86)");
  const char* localappdata = std::getenv("LOCALAPPDATA");

  if (pf) {
    candidates.push_back(std::string(pf) +
                         "\\Google\\Chrome\\Application\\chrome.exe");
  }
  if (pf86) {
    candidates.push_back(std::string(pf86) +
                         "\\Google\\Chrome\\Application\\chrome.exe");
  }
  if (localappdata) {
    candidates.push_back(std::string(localappdata) +
                         "\\Google\\Chrome\\Application\\chrome.exe");
    candidates.push_back(std::string(localappdata) +
                         "\\Chromium\\Application\\chrome.exe");
  }

  for (const auto& path : candidates) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }

  // Try `where` command
  auto result = platform::exec_capture("where chrome.exe 2>nul", 5);
  if (result.exit_code == 0 && !result.output.empty()) {
    auto line = result.output.substr(0, result.output.find('\n'));
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty())
      return line;
  }
#else
  // Check common Linux paths
  std::vector<std::string> candidates = {
      "/usr/bin/chromium-browser",
      "/usr/bin/chromium",
      "/usr/bin/google-chrome-stable",
      "/usr/bin/google-chrome",
      "/snap/bin/chromium",
  };

  for (const auto& path : candidates) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }

  // Try `which`
  auto result = platform::exec_capture(
      "which chromium-browser chromium google-chrome 2>/dev/null", 5);
  if (result.exit_code == 0 && !result.output.empty()) {
    auto line = result.output.substr(0, result.output.find('\n'));
    if (!line.empty() && line.back() == '\n')
      line.pop_back();
    if (!line.empty())
      return line;
  }
#endif

  return "";
}

bool BrowserSession::check_navigation(const std::string& url) const {
  std::regex url_re(R"(https?://([^/:]+))");
  std::smatch match;
  if (!std::regex_search(url, match, url_re)) {
    return true;
  }
  std::string host = match[1].str();
  return config_.ssrf_policy.is_allowed(host);
}

// --- browser_tools namespace ---

namespace browser_tools {

std::vector<nlohmann::json> get_tool_schemas() {
  return {
      {{"type", "function"},
       {"function",
        {{"name", "browser_navigate"},
         {"description", "Navigate the browser to a URL"},
         {"parameters",
          {{"type", "object"},
           {"properties",
            {{"url",
              {{"type", "string"}, {"description", "URL to navigate to"}}}}},
           {"required", nlohmann::json::array({"url"})}}}}}},
      {{"type", "function"},
       {"function",
        {{"name", "browser_screenshot"},
         {"description", "Take a screenshot of the current page"},
         {"parameters",
          {{"type", "object"},
           {"properties",
            {{"fullPage",
              {{"type", "boolean"},
               {"description", "Capture full scrollable page"},
               {"default", false}}}}}}}}}},
      {{"type", "function"},
       {"function",
        {{"name", "browser_click"},
         {"description", "Click an element on the page"},
         {"parameters",
          {{"type", "object"},
           {"properties",
            {{"selector",
              {{"type", "string"},
               {"description", "CSS selector of element to click"}}}}},
           {"required", nlohmann::json::array({"selector"})}}}}}},
      {{"type", "function"},
       {"function",
        {{"name", "browser_type"},
         {"description", "Type text into an input element"},
         {"parameters",
          {{"type", "object"},
           {"properties",
            {{"selector",
              {{"type", "string"},
               {"description", "CSS selector of input element"}}},
             {"text", {{"type", "string"}, {"description", "Text to type"}}}}},
           {"required", nlohmann::json::array({"selector", "text"})}}}}}},
      {{"type", "function"},
       {"function",
        {{"name", "browser_evaluate"},
         {"description", "Execute JavaScript in the browser page"},
         {"parameters",
          {{"type", "object"},
           {"properties",
            {{"expression",
              {{"type", "string"},
               {"description", "JavaScript expression to evaluate"}}}}},
           {"required", nlohmann::json::array({"expression"})}}}}}},
  };
}

std::function<std::string(const nlohmann::json&)>
create_executor(std::shared_ptr<BrowserSession> session) {
  return [session](const nlohmann::json& params) -> std::string {
    std::string action = params.value("action", "");

    if (action == "navigate" || params.contains("url")) {
      std::string url = params.value("url", "");
      bool ok = session->navigate(url);
      return ok ? R"({"success": true})" : R"({"error": "Navigation failed"})";
    }

    if (action == "screenshot") {
      bool full = params.value("fullPage", false);
      auto data = session->screenshot_base64(full);
      return R"({"data": ")" + data + R"("})";
    }

    if (action == "click") {
      std::string sel = params.value("selector", "");
      bool ok = session->click(sel);
      return ok ? R"({"success": true})" : R"({"error": "Click failed"})";
    }

    if (action == "type") {
      std::string sel = params.value("selector", "");
      std::string text = params.value("text", "");
      bool ok = session->type(sel, text);
      return ok ? R"({"success": true})" : R"({"error": "Type failed"})";
    }

    if (action == "evaluate" || params.contains("expression")) {
      std::string expr = params.value("expression", "");
      auto result = session->evaluate_js(expr);
      return result;
    }

    return R"({"error": "Unknown browser action"})";
  };
}

}  // namespace browser_tools

}  // namespace quantclaw
