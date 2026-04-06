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
                 int timeout, std::shared_ptr<spdlog::logger> logger,
                 std::string provider_id = "openai",
                 std::string api = "openai-completions");

  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& request) override;
  void ChatCompletionStream(
      const ChatCompletionRequest& request,
      std::function<void(const ChatCompletionResponse&)> callback) override;
  std::string GetProviderName() const override;
  std::vector<std::string> GetSupportedModels() const override;

 protected:
  virtual std::string ResolveApiKey() const;
  virtual std::string ResolveBaseUrl() const;
  virtual std::string ProviderId() const;
  std::string MakeApiRequest(const std::string& json_payload) const;
  virtual CurlSlist CreateHeaders() const;

 private:
  std::string api_key_;
  std::string base_url_;
  int timeout_;
  std::shared_ptr<spdlog::logger> logger_;
  std::string provider_id_;
  std::string api_;
};

}  // namespace quantclaw
