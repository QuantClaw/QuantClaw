// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace quantclaw {

struct ContentBlock {
  std::string type;  // "text" | "tool_use" | "tool_result" | "thinking"
  // For text/thinking
  std::string text;
  // For tool_use
  std::string id;
  std::string name;
  nlohmann::json input;
  // For tool_result
  std::string tool_use_id;
  std::string content;

  nlohmann::json ToJson() const;
  static ContentBlock FromJson(const nlohmann::json& j);
  static ContentBlock MakeText(const std::string& text);
  static ContentBlock MakeToolUse(const std::string& id,
                                  const std::string& name,
                                  const nlohmann::json& input);
  static ContentBlock MakeToolResult(const std::string& tool_use_id,
                                     const std::string& content);
};

}  // namespace quantclaw
