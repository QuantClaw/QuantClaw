// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <memory>
#include "quantclaw/core/content_block.hpp"

namespace quantclaw {

struct Message {
    std::string role;
    std::vector<ContentBlock> content;

    Message() = default;
    Message(std::string role, std::string text)
        : role(std::move(role)) {
        if (!text.empty()) content.push_back(ContentBlock::MakeText(std::move(text)));
    }
    std::string text() const {
        std::string r;
        for (const auto& b : content)
            if (b.type == "text" || b.type == "thinking") r += b.text;
        return r;
    }
};

struct ToolCall {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

struct ChatCompletionRequest {
    std::vector<Message> messages;
    std::string model;
    double temperature = 0.7;
    int max_tokens = 4096;
    std::vector<nlohmann::json> tools;
    bool tool_choice_auto = true;
    bool stream = false;
    std::string thinking = "off";  // "off" | "low" | "medium" | "high"
};

struct ChatCompletionResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    std::string finish_reason;
    bool is_stream_end = false;
};

class LLMProvider {
public:
    virtual ~LLMProvider() = default;
    
    virtual ChatCompletionResponse ChatCompletion(const ChatCompletionRequest& request) = 0;
    virtual void ChatCompletionStream(const ChatCompletionRequest& request,
                                     std::function<void(const ChatCompletionResponse&)> callback) = 0;
    virtual std::string GetProviderName() const = 0;
    virtual std::vector<std::string> GetSupportedModels() const = 0;
};

} // namespace quantclaw