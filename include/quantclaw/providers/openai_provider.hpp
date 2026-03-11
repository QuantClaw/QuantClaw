// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "quantclaw/providers/curl_raii.hpp"

#include "llm_provider.hpp"

namespace quantclaw {

class OpenAIProvider : public LLMProvider {
 public:
  OpenAIProvider(const std::string& api_key, const std::string& base_url,
                 int timeout, std::shared_ptr<spdlog::logger> logger);

  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& request) override;
  void ChatCompletionStream(
      const ChatCompletionRequest& request,
      std::function<void(const ChatCompletionResponse&)> callback) override;
  std::string GetProviderName() const override;
  std::vector<std::string> GetSupportedModels() const override;

 private:
  std::string MakeApiRequest(const std::string& json_payload) const;
  CurlSlist CreateHeaders() const;

  std::string api_key_;
  std::string base_url_;
  int timeout_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw
