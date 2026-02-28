#include "quantclaw/tools/browser_tool.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace quantclaw {

// --- SsrfPolicy ---

bool SsrfPolicy::is_allowed(const std::string& host) const {
  // Check blocked hosts
  for (const auto& blocked : blocked_hosts) {
    if (host == blocked) return false;
  }

  // Check blocked ranges (simplified: just check common private prefixes)
  for (const auto& range : blocked_ranges) {
    if (range == "10.0.0.0/8" && host.substr(0, 3) == "10.") return false;
    if (range == "172.16.0.0/12" && host.substr(0, 4) == "172.") {
      // Simplified check
      return false;
    }
    if (range == "192.168.0.0/16" && host.substr(0, 8) == "192.168.") {
      return false;
    }
  }

  // Check allowed hosts (whitelist mode)
  if (!allowed_hosts.empty()) {
    for (const auto& allowed : allowed_hosts) {
      if (host == allowed) return true;
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

BrowserToolConfig BrowserToolConfig::from_json(const nlohmann::json& j) {
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

  if (j.contains("ssrf") && j["ssrf"].is_object()) {
    auto& ssrf = j["ssrf"];
    if (ssrf.contains("blockedHosts") && ssrf["blockedHosts"].is_array()) {
      for (const auto& h : ssrf["blockedHosts"]) {
        if (h.is_string()) c.ssrf_policy.blocked_hosts.push_back(h);
      }
    }
    if (ssrf.contains("allowedHosts") && ssrf["allowedHosts"].is_array()) {
      for (const auto& h : ssrf["allowedHosts"]) {
        if (h.is_string()) c.ssrf_policy.allowed_hosts.push_back(h);
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

BrowserSession::~BrowserSession() { close(); }

bool BrowserSession::initialize(const BrowserToolConfig& config) {
  config_ = config;
  if (config_.mode == BrowserToolConfig::Mode::kRemote) {
    return connect_remote();
  }
  return launch_local();
}

void BrowserSession::close() {
  if (browser_pid_ > 0) {
    kill(browser_pid_, SIGTERM);
    int status;
    waitpid(browser_pid_, &status, WNOHANG);
    browser_pid_ = 0;
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
  if (result.empty()) return false;

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

std::string BrowserSession::get_page_text() const {
  // Would use CDP to get document text; simplified stub
  return "";
}

std::string BrowserSession::get_accessibility_tree() const {
  // Would use CDP to get accessibility snapshot; simplified stub
  return "";
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

  // Build args
  std::vector<std::string> args = {
      chromium,
      "--remote-debugging-port=0",  // Let OS pick port
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

  // Fork and exec
  pid_t pid = fork();
  if (pid == 0) {
    // Child
    std::vector<char*> c_args;
    for (auto& a : args) c_args.push_back(const_cast<char*>(a.c_str()));
    c_args.push_back(nullptr);
    execvp(c_args[0], c_args.data());
    _exit(127);
  } else if (pid > 0) {
    browser_pid_ = pid;
    connection_.pid_or_id = std::to_string(pid);
    connection_.is_remote = false;
    connection_.is_running = true;
    logger_->info("Launched browser: PID={}, binary={}", pid, chromium);
    return true;
  }

  logger_->error("Failed to fork browser process");
  return false;
}

bool BrowserSession::connect_remote() {
  if (config_.remote_cdp_url.empty()) {
    logger_->error("No remote CDP URL configured");
    return false;
  }

  connection_.cdp_url = config_.remote_cdp_url;
  connection_.is_remote = true;
  connection_.is_running = true;
  logger_->info("Connected to remote browser: {}", config_.remote_cdp_url);
  return true;
}

std::string BrowserSession::cdp_send(const std::string& method,
                                      const nlohmann::json& params) {
  // In production, this would use WebSocket to send CDP commands.
  // This is a placeholder implementation.
  logger_->debug("CDP: {} params={}", method, params.dump());

  // For now, return empty indicating the command was "sent"
  // Full CDP WebSocket implementation would go here.
  return "{}";
}

std::string BrowserSession::find_chromium() {
  // Check common paths on Linux
  std::vector<std::string> candidates = {
      "/usr/bin/chromium-browser",
      "/usr/bin/chromium",
      "/usr/bin/google-chrome-stable",
      "/usr/bin/google-chrome",
      "/snap/bin/chromium",
  };

  for (const auto& path : candidates) {
    if (access(path.c_str(), X_OK) == 0) {
      return path;
    }
  }

  // Try `which`
  FILE* pipe = popen("which chromium-browser chromium google-chrome 2>/dev/null", "r");
  if (pipe) {
    char buf[256];
    if (fgets(buf, sizeof(buf), pipe)) {
      std::string result(buf);
      // Remove trailing newline
      if (!result.empty() && result.back() == '\n') result.pop_back();
      pclose(pipe);
      if (!result.empty()) return result;
    } else {
      pclose(pipe);
    }
  }

  return "";
}

bool BrowserSession::check_navigation(const std::string& url) const {
  // Extract host from URL
  std::regex url_re(R"(https?://([^/:]+))");
  std::smatch match;
  if (!std::regex_search(url, match, url_re)) {
    return true;  // Non-HTTP URLs pass through
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
            {{"url", {{"type", "string"}, {"description", "URL to navigate to"}}}}},
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
             {"text",
              {{"type", "string"}, {"description", "Text to type"}}}}},
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
