// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/providers/llm_provider.hpp"

namespace quantclaw {

struct StreamNormalizationContext {
  std::string provider_id;
  std::string api;
  std::vector<std::string> allowed_tool_names;
  std::unordered_map<std::string, std::string> normalized_allowed_tool_names;
  std::unordered_map<std::string, std::string> folded_allowed_tool_names;
  bool decode_html_entities = false;
  std::shared_ptr<spdlog::logger> logger;
};

struct PendingToolCallFragment {
  size_t index = 0;
  std::string id;
  std::string name;
  std::string arguments;
};

std::vector<std::string>
ExtractAllowedToolNames(const ChatCompletionRequest& request);

StreamNormalizationContext
BuildStreamNormalizationContext(const std::string& provider_id,
                                const std::string& api,
                                const ChatCompletionRequest& request,
                                const std::shared_ptr<spdlog::logger>& logger);

void SanitizeReplayMessages(std::vector<Message>* messages,
                            const StreamNormalizationContext& context);

void NormalizeToolCalls(std::vector<ToolCall>* tool_calls,
                        const StreamNormalizationContext& context,
                        std::unordered_set<std::string>* seen_ids = nullptr);

std::vector<ToolCall> FinalizePendingToolCalls(
    const std::vector<PendingToolCallFragment>& pending,
    const StreamNormalizationContext& context,
    std::unordered_set<std::string>* seen_ids = nullptr,
    std::vector<PendingToolCallFragment>* remaining = nullptr);

}  // namespace quantclaw
