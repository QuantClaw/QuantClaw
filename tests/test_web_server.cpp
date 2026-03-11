// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <memory>
#include <thread>

#include <httplib.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/web/web_server.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class WebServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);
  }

  void TearDown() override {
    if (server_) {
      server_->Stop();
      server_.reset();
    }
  }

  int find_free_port() {
    return quantclaw::test::FindFreePort();
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::web::WebServer> server_;
};

TEST_F(WebServerTest, HealthEndpoint) {
  int port = find_free_port();
  server_ = std::make_unique<quantclaw::web::WebServer>(port, logger_);
  server_->Start();

  // Wait for server to be ready
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  httplib::Client cli("127.0.0.1", port);
  auto res = cli.Get("/health");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["status"], "ok");
}

TEST_F(WebServerTest, CustomGetRoute) {
  int port = find_free_port();
  server_ = std::make_unique<quantclaw::web::WebServer>(port, logger_);

  server_->AddRoute("/api/test", "GET",
                    [](const std::string& /*method*/,
                       const std::string& /*body*/) -> std::string {
                      return R"({"result":"hello"})";
                    });

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  httplib::Client cli("127.0.0.1", port);
  auto res = cli.Get("/api/test");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["result"], "hello");
}

TEST_F(WebServerTest, CustomPostRoute) {
  int port = find_free_port();
  server_ = std::make_unique<quantclaw::web::WebServer>(port, logger_);

  server_->AddRoute("/api/echo", "POST",
                    [](const std::string& /*method*/,
                       const std::string& body) -> std::string {
                      auto j = nlohmann::json::parse(body);
                      return nlohmann::json({{"echo", j["msg"]}}).dump();
                    });

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  httplib::Client cli("127.0.0.1", port);
  auto res = cli.Post("/api/echo", R"({"msg":"world"})", "application/json");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["echo"], "world");
}

TEST_F(WebServerTest, StartAndStop) {
  int port = find_free_port();
  server_ = std::make_unique<quantclaw::web::WebServer>(port, logger_);

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify it's reachable
  httplib::Client cli("127.0.0.1", port);
  auto res = cli.Get("/health");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  // Stop
  server_->Stop();
  server_.reset();

  // After stop, connection should fail
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  httplib::Client cli2("127.0.0.1", port);
  cli2.set_connection_timeout(1);
  auto res2 = cli2.Get("/health");
  EXPECT_FALSE(res2);
}

TEST_F(WebServerTest, ResponseContentType) {
  int port = find_free_port();
  server_ = std::make_unique<quantclaw::web::WebServer>(port, logger_);
  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  httplib::Client cli("127.0.0.1", port);
  auto res = cli.Get("/health");

  ASSERT_TRUE(res);
  EXPECT_TRUE(res->get_header_value("Content-Type").find("application/json") !=
              std::string::npos);
}
