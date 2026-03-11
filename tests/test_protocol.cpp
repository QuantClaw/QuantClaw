// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/protocol.hpp"

#include <gtest/gtest.h>

using namespace quantclaw::gateway;

// --- Frame type conversion ---

TEST(ProtocolTest, FrameTypeToString) {
  EXPECT_EQ(FrameTypeToString(FrameType::kRequest), "req");
  EXPECT_EQ(FrameTypeToString(FrameType::kResponse), "res");
  EXPECT_EQ(FrameTypeToString(FrameType::kEvent), "event");
}

TEST(ProtocolTest, FrameTypeFromString) {
  EXPECT_EQ(FrameTypeFromString("req"), FrameType::kRequest);
  EXPECT_EQ(FrameTypeFromString("res"), FrameType::kResponse);
  EXPECT_EQ(FrameTypeFromString("event"), FrameType::kEvent);
  EXPECT_THROW(FrameTypeFromString("invalid"), std::runtime_error);
}

// --- RpcRequest ---

TEST(ProtocolTest, RpcRequestToJson) {
  RpcRequest req;
  req.id = "42";
  req.method = "gateway.health";
  req.params = {{"key", "value"}};

  auto j = req.ToJson();
  EXPECT_EQ(j["type"], "req");
  EXPECT_EQ(j["id"], "42");
  EXPECT_EQ(j["method"], "gateway.health");
  EXPECT_EQ(j["params"]["key"], "value");
}

TEST(ProtocolTest, RpcRequestFromJson) {
  nlohmann::json j = {{"type", "req"},
                      {"id", "7"},
                      {"method", "sessions.list"},
                      {"params", {{"limit", 10}}}};

  auto req = RpcRequest::FromJson(j);
  EXPECT_EQ(req.id, "7");
  EXPECT_EQ(req.method, "sessions.list");
  EXPECT_EQ(req.params["limit"], 10);
}

TEST(ProtocolTest, RpcRequestFromJsonNoParams) {
  nlohmann::json j = {
      {"type", "req"}, {"id", "1"}, {"method", "gateway.health"}};

  auto req = RpcRequest::FromJson(j);
  EXPECT_EQ(req.method, "gateway.health");
  EXPECT_TRUE(req.params.is_object());
  EXPECT_TRUE(req.params.empty());
}

// --- RpcResponse ---

TEST(ProtocolTest, RpcResponseSuccess) {
  auto resp = RpcResponse::success("42", {{"status", "ok"}});

  EXPECT_EQ(resp.id, "42");
  EXPECT_TRUE(resp.ok);
  EXPECT_EQ(resp.payload["status"], "ok");

  auto j = resp.ToJson();
  EXPECT_EQ(j["type"], "res");
  EXPECT_EQ(j["id"], "42");
  EXPECT_TRUE(j["ok"]);
  EXPECT_EQ(j["payload"]["status"], "ok");
  EXPECT_FALSE(j.contains("error"));
}

TEST(ProtocolTest, RpcResponseFailure) {
  auto resp = RpcResponse::failure("99", "Not found", "NOT_FOUND");

  EXPECT_EQ(resp.id, "99");
  EXPECT_FALSE(resp.ok);
  EXPECT_EQ(resp.error.message, "Not found");
  EXPECT_EQ(resp.error.code, "NOT_FOUND");
  EXPECT_FALSE(resp.error.retryable);

  auto j = resp.ToJson();
  EXPECT_EQ(j["type"], "res");
  EXPECT_FALSE(j["ok"]);
  EXPECT_TRUE(j["error"].is_object());
  EXPECT_EQ(j["error"]["code"], "NOT_FOUND");
  EXPECT_EQ(j["error"]["message"], "Not found");
  EXPECT_FALSE(j["error"]["retryable"]);
  EXPECT_FALSE(j.contains("payload"));
}

TEST(ProtocolTest, RpcResponseFailureWithRetry) {
  auto resp =
      RpcResponse::failure("42", "Rate limited", "RATE_LIMITED", true, 5000);

  EXPECT_EQ(resp.error.code, "RATE_LIMITED");
  EXPECT_TRUE(resp.error.retryable);
  EXPECT_EQ(resp.error.retry_after_ms, 5000);

  auto j = resp.ToJson();
  EXPECT_TRUE(j["error"]["retryable"]);
  EXPECT_EQ(j["error"]["retryAfterMs"], 5000);
}

TEST(ProtocolTest, RpcResponseFailureDefaultCode) {
  auto resp = RpcResponse::failure("1", "Something broke");
  EXPECT_EQ(resp.error.code, "INTERNAL_ERROR");
  EXPECT_EQ(resp.error.message, "Something broke");
}

// --- RpcEvent ---

TEST(ProtocolTest, RpcEventToJson) {
  RpcEvent evt;
  evt.event = "agent.text_delta";
  evt.payload = {{"text", "hello"}};
  evt.seq = 5;

  auto j = evt.ToJson();
  EXPECT_EQ(j["type"], "event");
  EXPECT_EQ(j["event"], "agent.text_delta");
  EXPECT_EQ(j["payload"]["text"], "hello");
  EXPECT_EQ(j["seq"], 5);
  EXPECT_FALSE(j.contains("stateVersion"));
}

TEST(ProtocolTest, RpcEventWithStateVersion) {
  RpcEvent evt;
  evt.event = "gateway.tick";
  evt.payload = {};
  evt.state_version = 12;

  auto j = evt.ToJson();
  EXPECT_EQ(j["stateVersion"], 12);
}

TEST(ProtocolTest, RpcEventNoOptionals) {
  RpcEvent evt;
  evt.event = "test";
  evt.payload = {};

  auto j = evt.ToJson();
  EXPECT_FALSE(j.contains("seq"));
  EXPECT_FALSE(j.contains("stateVersion"));
}

// --- ConnectChallenge ---

TEST(ProtocolTest, ConnectChallengeToJson) {
  ConnectChallenge challenge;
  challenge.nonce = "abc123";
  challenge.timestamp = 1700000000;

  auto j = challenge.ToJson();
  EXPECT_EQ(j["type"], "event");
  EXPECT_EQ(j["event"], "connect.challenge");
  EXPECT_EQ(j["payload"]["nonce"], "abc123");
  EXPECT_EQ(j["payload"]["ts"], 1700000000);
}

// --- ConnectHelloParams ---

TEST(ProtocolTest, ConnectHelloParamsFromJson) {
  nlohmann::json j = {{"minProtocol", 1},
                      {"maxProtocol", 2},
                      {"clientName", "test-client"},
                      {"clientVersion", "1.0.0"},
                      {"role", "operator"},
                      {"scopes", {"operator.read", "operator.write"}},
                      {"authToken", "secret"},
                      {"deviceId", "dev-001"}};

  auto params = ConnectHelloParams::FromJson(j);
  EXPECT_EQ(params.min_protocol, 1);
  EXPECT_EQ(params.max_protocol, 2);
  EXPECT_EQ(params.client_name, "test-client");
  EXPECT_EQ(params.client_version, "1.0.0");
  EXPECT_EQ(params.role, "operator");
  EXPECT_EQ(params.scopes.size(), 2u);
  EXPECT_EQ(params.auth_token, "secret");
  EXPECT_EQ(params.device_id, "dev-001");
}

TEST(ProtocolTest, ConnectHelloParamsDefaults) {
  auto params = ConnectHelloParams::FromJson(nlohmann::json::object());
  EXPECT_EQ(params.min_protocol, 1);
  EXPECT_EQ(params.max_protocol, 3);
  EXPECT_EQ(params.role, "operator");
  EXPECT_EQ(params.scopes.size(), 2u);
}

// --- HelloOkPayload ---

TEST(ProtocolTest, HelloOkPayloadToJson) {
  HelloOkPayload payload;
  payload.policy = "permissive";
  payload.authenticated = true;
  payload.tick_interval_ms = 15000;
  payload.conn_id = "conn-abc";

  auto j = payload.ToJson();
  EXPECT_EQ(j["protocol"], 3);
  EXPECT_EQ(j["policy"], "permissive");
  EXPECT_TRUE(j["authenticated"]);
  EXPECT_EQ(j["tickIntervalMs"], 15000);

  // Server info
  EXPECT_TRUE(j.contains("server"));
  EXPECT_EQ(j["server"]["version"], "0.2.0");
  EXPECT_EQ(j["server"]["connId"], "conn-abc");

  // Features
  EXPECT_TRUE(j.contains("features"));
  EXPECT_TRUE(j["features"]["methods"].is_array());
  EXPECT_TRUE(j["features"]["events"].is_array());
  EXPECT_GT(j["features"]["methods"].size(), 0u);
  EXPECT_GT(j["features"]["events"].size(), 0u);
}

TEST(ProtocolTest, HelloOkPayloadOpenClawFormat) {
  HelloOkPayload payload;
  payload.openclaw_format = true;
  payload.conn_id = "oc-123";

  auto j = payload.ToJson();
  EXPECT_EQ(j["protocol"], 3);
  EXPECT_TRUE(j.contains("server"));
  EXPECT_TRUE(j.contains("features"));
  EXPECT_TRUE(j.contains("capabilities"));
  EXPECT_TRUE(j.contains("policy"));
  EXPECT_TRUE(j["policy"].is_object());
  EXPECT_EQ(j["policy"]["maxPayload"], 1048576);
}

TEST(ProtocolTest, HelloOkPayloadWithSnapshot) {
  HelloOkPayload payload;
  payload.conn_id = "conn-snap";
  payload.snapshot = {{"presence", nlohmann::json::array()},
                      {"uptimeMs", 5000},
                      {"authMode", "none"}};

  auto j = payload.ToJson();
  EXPECT_TRUE(j.contains("snapshot"));
  EXPECT_EQ(j["snapshot"]["uptimeMs"], 5000);
  EXPECT_EQ(j["snapshot"]["authMode"], "none");
}

TEST(ProtocolTest, HelloOkPayloadNoSnapshotWhenNull) {
  HelloOkPayload payload;
  payload.conn_id = "conn-no-snap";
  // snapshot left as default (null)

  auto j = payload.ToJson();
  EXPECT_FALSE(j.contains("snapshot"));
}

// --- parse_frame_type ---

TEST(ProtocolTest, ParseFrameType) {
  EXPECT_EQ(ParseFrameType({{"type", "req"}}), FrameType::kRequest);
  EXPECT_EQ(ParseFrameType({{"type", "res"}}), FrameType::kResponse);
  EXPECT_EQ(ParseFrameType({{"type", "event"}}), FrameType::kEvent);
}

// --- Method / Event constants ---

TEST(ProtocolTest, MethodConstants) {
  EXPECT_STREQ(methods::kConnectHello, "connect.hello");
  EXPECT_STREQ(methods::kGatewayHealth, "gateway.health");
  EXPECT_STREQ(methods::kGatewayStatus, "gateway.status");
  EXPECT_STREQ(methods::kConfigGet, "config.get");
  EXPECT_STREQ(methods::kAgentRequest, "agent.request");
  EXPECT_STREQ(methods::kAgentStop, "agent.stop");
  EXPECT_STREQ(methods::kSessionsList, "sessions.list");
  EXPECT_STREQ(methods::kSessionsHistory, "sessions.history");
  EXPECT_STREQ(methods::kChannelsList, "channels.list");
}

TEST(ProtocolTest, EventConstants) {
  EXPECT_STREQ(events::kConnectChallenge, "connect.challenge");
  EXPECT_STREQ(events::kTextDelta, "agent.text_delta");
  EXPECT_STREQ(events::kToolUse, "agent.tool_use");
  EXPECT_STREQ(events::kToolResult, "agent.tool_result");
  EXPECT_STREQ(events::kMessageEnd, "agent.message_end");
  EXPECT_STREQ(events::kTick, "gateway.tick");
}

// --- Roundtrip serialization ---

TEST(ProtocolTest, RequestRoundtrip) {
  RpcRequest original;
  original.id = "test-id";
  original.method = "test.method";
  original.params = {{"foo", "bar"}, {"num", 42}};

  auto j = original.ToJson();
  auto parsed = RpcRequest::FromJson(j);

  EXPECT_EQ(parsed.id, original.id);
  EXPECT_EQ(parsed.method, original.method);
  EXPECT_EQ(parsed.params, original.params);
}
