// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

export module quantclaw.core.content_block;

import std;
import nlohmann.json;

export namespace quantclaw {

struct ContentBlock {
  std::string type;  // "text" | "tool_use" | "tool_result" | "thinking"
  std::string text;
  std::string id;
  std::string name;
  nlohmann::json input;
  std::string tool_use_id;
  std::string content;

  nlohmann::json ToJson() const {
    nlohmann::json j;
    j["type"] = type;
    if (type == "text" || type == "thinking") {
      j["text"] = text;
    } else if (type == "tool_use") {
      j["id"] = id;
      j["name"] = name;
      j["input"] = input;
    } else if (type == "tool_result") {
      j["tool_use_id"] = tool_use_id;
      j["content"] = content;
    }
    return j;
  }

  static ContentBlock FromJson(const nlohmann::json& j) {
    ContentBlock cb;
    cb.type = j.value("type", "text");
    if (cb.type == "text" || cb.type == "thinking") {
      cb.text = j.value("text", "");
    } else if (cb.type == "tool_use") {
      cb.id = j.value("id", "");
      cb.name = j.value("name", "");
      cb.input = j.value("input", nlohmann::json::object());
    } else if (cb.type == "tool_result") {
      cb.tool_use_id = j.value("tool_use_id", "");
      cb.content = j.value("content", "");
    }
    return cb;
  }

  static ContentBlock MakeText(const std::string& text) {
    ContentBlock cb;
    cb.type = "text";
    cb.text = text;
    return cb;
  }

  static ContentBlock MakeToolUse(const std::string& id,
                                  const std::string& name,
                                  const nlohmann::json& input) {
    ContentBlock cb;
    cb.type = "tool_use";
    cb.id = id;
    cb.name = name;
    cb.input = input;
    return cb;
  }

  static ContentBlock MakeToolResult(const std::string& tool_use_id,
                                     const std::string& content) {
    ContentBlock cb;
    cb.type = "tool_result";
    cb.tool_use_id = tool_use_id;
    cb.content = content;
    return cb;
  }
};

}  // namespace quantclaw
