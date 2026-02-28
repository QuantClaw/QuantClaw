#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include "quantclaw/tools/browser_tool.hpp"

namespace quantclaw {

static std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

// --- SsrfPolicy tests ---

TEST(SsrfPolicyTest, DefaultBlocksLocalhost) {
  auto p = SsrfPolicy::default_policy();
  EXPECT_FALSE(p.is_allowed("localhost"));
  EXPECT_FALSE(p.is_allowed("127.0.0.1"));
  EXPECT_FALSE(p.is_allowed("0.0.0.0"));
}

TEST(SsrfPolicyTest, DefaultBlocksPrivateRanges) {
  auto p = SsrfPolicy::default_policy();
  EXPECT_FALSE(p.is_allowed("10.0.0.1"));
  EXPECT_FALSE(p.is_allowed("192.168.1.1"));
}

TEST(SsrfPolicyTest, AllowsPublicHosts) {
  auto p = SsrfPolicy::default_policy();
  EXPECT_TRUE(p.is_allowed("example.com"));
  EXPECT_TRUE(p.is_allowed("api.openai.com"));
  EXPECT_TRUE(p.is_allowed("8.8.8.8"));
}

TEST(SsrfPolicyTest, WhitelistMode) {
  SsrfPolicy p;
  p.allowed_hosts = {"trusted.com", "api.example.com"};

  EXPECT_TRUE(p.is_allowed("trusted.com"));
  EXPECT_TRUE(p.is_allowed("api.example.com"));
  EXPECT_FALSE(p.is_allowed("evil.com"));
  EXPECT_FALSE(p.is_allowed("other.com"));
}

TEST(SsrfPolicyTest, EmptyPolicyAllowsAll) {
  SsrfPolicy p;
  EXPECT_TRUE(p.is_allowed("localhost"));
  EXPECT_TRUE(p.is_allowed("example.com"));
  EXPECT_TRUE(p.is_allowed("10.0.0.1"));
}

// --- BrowserToolConfig tests ---

TEST(BrowserToolConfigTest, FromJson) {
  nlohmann::json j = {
      {"mode", "remote"},
      {"cdpUrl", "ws://localhost:9222"},
      {"headless", false},
      {"viewportWidth", 1920},
      {"viewportHeight", 1080},
      {"navigationTimeoutMs", 60000},
  };
  auto c = BrowserToolConfig::from_json(j);
  EXPECT_EQ(c.mode, BrowserToolConfig::Mode::kRemote);
  EXPECT_EQ(c.remote_cdp_url, "ws://localhost:9222");
  EXPECT_FALSE(c.headless);
  EXPECT_EQ(c.viewport_width, 1920);
  EXPECT_EQ(c.viewport_height, 1080);
  EXPECT_EQ(c.navigation_timeout_ms, 60000);
}

TEST(BrowserToolConfigTest, Defaults) {
  auto c = BrowserToolConfig::from_json(nlohmann::json::object());
  EXPECT_EQ(c.mode, BrowserToolConfig::Mode::kLocal);
  EXPECT_TRUE(c.headless);
  EXPECT_EQ(c.viewport_width, 1280);
  EXPECT_EQ(c.viewport_height, 720);
}

TEST(BrowserToolConfigTest, SsrfConfig) {
  nlohmann::json j = {
      {"ssrf",
       {{"blockedHosts", nlohmann::json::array({"evil.com"})},
        {"allowedHosts", nlohmann::json::array({"safe.com"})}}},
  };
  auto c = BrowserToolConfig::from_json(j);
  EXPECT_EQ(c.ssrf_policy.blocked_hosts.size(), 1);
  EXPECT_EQ(c.ssrf_policy.allowed_hosts.size(), 1);
}

// --- BrowserSession tests ---

TEST(BrowserSessionTest, CreateAndClose) {
  auto session = std::make_shared<BrowserSession>(make_logger("browser"));
  EXPECT_FALSE(session->is_connected());
  session->close();  // Should not crash
}

TEST(BrowserSessionTest, RemoteConnection) {
  auto session = std::make_shared<BrowserSession>(make_logger("browser"));
  BrowserToolConfig config;
  config.mode = BrowserToolConfig::Mode::kRemote;
  config.remote_cdp_url = "ws://localhost:9222/devtools/browser/fake-id";

  bool ok = session->initialize(config);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(session->is_connected());
  EXPECT_EQ(session->connection().cdp_url, config.remote_cdp_url);
  EXPECT_TRUE(session->connection().is_remote);

  session->close();
  EXPECT_FALSE(session->is_connected());
}

TEST(BrowserSessionTest, RemoteRequiresUrl) {
  auto session = std::make_shared<BrowserSession>(make_logger("browser"));
  BrowserToolConfig config;
  config.mode = BrowserToolConfig::Mode::kRemote;
  // No URL set

  bool ok = session->initialize(config);
  EXPECT_FALSE(ok);
}

TEST(BrowserSessionTest, NavigationSsrfBlock) {
  auto session = std::make_shared<BrowserSession>(make_logger("browser"));
  BrowserToolConfig config;
  config.mode = BrowserToolConfig::Mode::kRemote;
  config.remote_cdp_url = "ws://test:9222";
  config.ssrf_policy = SsrfPolicy::default_policy();
  session->initialize(config);

  // localhost should be blocked
  EXPECT_FALSE(session->navigate("http://localhost:8080/admin"));
  EXPECT_FALSE(session->navigate("http://127.0.0.1/api"));

  // Public URLs should pass (CDP call is stubbed)
  EXPECT_TRUE(session->navigate("https://example.com"));
}

// --- Tool schema tests ---

TEST(BrowserToolSchemaTest, HasAllTools) {
  auto schemas = browser_tools::get_tool_schemas();
  EXPECT_EQ(schemas.size(), 5);

  std::vector<std::string> expected_names = {
      "browser_navigate", "browser_screenshot", "browser_click",
      "browser_type", "browser_evaluate",
  };
  for (const auto& schema : schemas) {
    auto name = schema["function"]["name"].get<std::string>();
    EXPECT_TRUE(std::find(expected_names.begin(), expected_names.end(), name) !=
                expected_names.end())
        << "Unexpected tool: " << name;
  }
}

TEST(BrowserToolSchemaTest, ExecutorCreation) {
  auto session = std::make_shared<BrowserSession>(make_logger("browser"));
  BrowserToolConfig config;
  config.mode = BrowserToolConfig::Mode::kRemote;
  config.remote_cdp_url = "ws://test:9222";
  session->initialize(config);

  auto executor = browser_tools::create_executor(session);
  ASSERT_TRUE(executor);

  // Test navigate action
  nlohmann::json params = {{"action", "navigate"}, {"url", "https://example.com"}};
  auto result = executor(params);
  EXPECT_FALSE(result.empty());

  // Test evaluate action
  params = {{"action", "evaluate"}, {"expression", "1+1"}};
  result = executor(params);
  EXPECT_FALSE(result.empty());

  // Test unknown action
  params = {{"action", "unknown"}};
  result = executor(params);
  EXPECT_TRUE(result.find("error") != std::string::npos);
}

TEST(BrowserSessionTest, PageState) {
  auto session = std::make_shared<BrowserSession>(make_logger("browser"));
  auto state = session->get_state();
  EXPECT_TRUE(state.url.empty());
  EXPECT_TRUE(state.title.empty());
  EXPECT_EQ(state.request_count, 0);
}

}  // namespace quantclaw
