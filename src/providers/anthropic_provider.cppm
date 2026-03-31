// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;
#include <cctype>
#include <curl/curl.h>
#include <spdlog/spdlog.h>

export module quantclaw.providers.anthropic_provider;

import std;
import nlohmann.json;

import quantclaw.providers.curl_raii;
import quantclaw.providers.llm_provider;
import quantclaw.providers.provider_error;

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                     std::string* userp) {
  userp->append(static_cast<char*>(contents), size * nmemb);
  return size * nmemb;
}

struct RetryAfterCapture {
  int retry_after_seconds = 0;
};

size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                      void* userdata) {
  size_t total = size * nitems;
  auto* capture = static_cast<RetryAfterCapture*>(userdata);
  std::string header(buffer, total);

  if (header.size() > 12) {
    std::string lower = header.substr(0, 12);
    for (char& c : lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "retry-after:") {
      std::string value = header.substr(12);
      auto start = value.find_first_not_of(" \t");
      if (start != std::string::npos) {
        value = value.substr(start);
      }
      try {
        capture->retry_after_seconds = std::stoi(value);
      } catch (...) {
      }
    }
  }
  return total;
}

std::pair<std::string, nlohmann::json> SerializeMessagesToAnthropic(
    const std::vector<quantclaw::Message>& messages) {
  std::string system_prompt;
  nlohmann::json arr = nlohmann::json::array();

  for (const auto& msg : messages) {
    if (msg.role == "system") {
      if (!system_prompt.empty())
        system_prompt += "\n";
      system_prompt += msg.text();
      continue;
    }

    nlohmann::json content_arr = nlohmann::json::array();
    for (const auto& b : msg.content) {
      if (b.type == "text" || b.type == "thinking") {
        if (!b.text.empty()) {
          content_arr.push_back({{"type", "text"}, {"text", b.text}});
        }
      } else if (b.type == "tool_use") {
        content_arr.push_back({{"type", "tool_use"},
                               {"id", b.id},
                               {"name", b.name},
                               {"input", b.input}});
      } else if (b.type == "tool_result") {
        content_arr.push_back({{"type", "tool_result"},
                               {"tool_use_id", b.tool_use_id},
                               {"content", b.content}});
      }
    }

    if (content_arr.empty())
      continue;

    std::string role = msg.role;
    if (role == "tool")
      role = "user";

    arr.push_back({{"role", role}, {"content", content_arr}});
  }

  return {system_prompt, arr};
}

int ThinkingBudgetTokens(const std::string& level) {
  if (level == "low")
    return 1024;
  if (level == "medium")
    return 4096;
  if (level == "high")
    return 16000;
  return 0;
}

void ApplyThinkingParams(nlohmann::json& payload,
                         const quantclaw::ChatCompletionRequest& request) {
  int budget = ThinkingBudgetTokens(request.thinking);
  if (budget > 0) {
    payload["thinking"] = {{"type", "enabled"}, {"budget_tokens", budget}};
    payload["temperature"] = 1;
    if (request.max_tokens <= budget) {
      payload["max_tokens"] = budget + 4096;
    }
  }
}

nlohmann::json ConvertToolsToAnthropic(const std::vector<nlohmann::json>& tools) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& tool : tools) {
    if (tool.value("type", "") == "function" && tool.contains("function")) {
      const auto& fn = tool["function"];
      nlohmann::json anthropic_tool;
      anthropic_tool["name"] = fn.value("name", "");
      anthropic_tool["description"] = fn.value("description", "");
      if (fn.contains("parameters")) {
        anthropic_tool["input_schema"] = fn["parameters"];
      } else {
        anthropic_tool["input_schema"] = {
            {"type", "object"}, {"properties", nlohmann::json::object()}};
      }
      arr.push_back(anthropic_tool);
    }
  }
  return arr;
}

struct AnthropicStreamContext {
  std::function<void(const quantclaw::ChatCompletionResponse&)> callback;
  std::string buffer;
  std::string event_type;
  std::shared_ptr<spdlog::logger> logger;

  struct PendingToolCall {
    std::string id;
    std::string name;
    std::string arguments;
  };

  std::vector<PendingToolCall> pending_tool_calls;
};

size_t AnthropicStreamWriteCallback(void* contents, size_t size, size_t nmemb,
                                    void* userp) {
  auto* ctx = static_cast<AnthropicStreamContext*>(userp);
  size_t total = size * nmemb;
  ctx->buffer.append(static_cast<char*>(contents), total);

  size_t pos;
  while ((pos = ctx->buffer.find('\n')) != std::string::npos) {
    std::string line = ctx->buffer.substr(0, pos);
    ctx->buffer.erase(0, pos + 1);

    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty())
      continue;

    if (line.rfind("event:", 0) == 0) {
      ctx->event_type = line.substr(6);
      if (!ctx->event_type.empty() && ctx->event_type.front() == ' ') {
        ctx->event_type.erase(0, 1);
      }
      continue;
    }

    if (line.rfind("data:", 0) != 0)
      continue;

    std::string data = line.substr(5);
    if (!data.empty() && data.front() == ' ') {
      data.erase(0, 1);
    }

    auto j = nlohmann::json::parse(data, nullptr, false);
    if (j.is_discarded())
      continue;

    if (ctx->event_type == "content_block_start") {
      if (j.contains("content_block")) {
        const auto& block = j["content_block"];
        if (block.value("type", "") == "tool_use") {
          AnthropicStreamContext::PendingToolCall ptc;
          ptc.id = block.value("id", "");
          ptc.name = block.value("name", "");
          ctx->pending_tool_calls.push_back(std::move(ptc));
        }
      }
    } else if (ctx->event_type == "content_block_delta") {
      if (j.contains("delta")) {
        const auto& delta = j["delta"];
        std::string delta_type = delta.value("type", "");

        if (delta_type == "text_delta") {
          quantclaw::ChatCompletionResponse resp;
          resp.content = delta.value("text", "");
          ctx->callback(resp);
        } else if (delta_type == "input_json_delta") {
          if (!ctx->pending_tool_calls.empty()) {
            ctx->pending_tool_calls.back().arguments +=
                delta.value("partial_json", "");
          }
        }
      }
    } else if (ctx->event_type == "message_stop") {
      if (!ctx->pending_tool_calls.empty()) {
        quantclaw::ChatCompletionResponse tc_resp;
        tc_resp.finish_reason = "tool_calls";
        for (const auto& ptc : ctx->pending_tool_calls) {
          quantclaw::ToolCall tc;
          tc.id = ptc.id;
          tc.name = ptc.name;
          tc.arguments = nlohmann::json::parse(ptc.arguments, nullptr, false);
          if (tc.arguments.is_discarded()) {
            tc.arguments = nlohmann::json::object();
          }
          tc_resp.tool_calls.push_back(tc);
        }
        ctx->pending_tool_calls.clear();
        ctx->callback(tc_resp);
      }

      quantclaw::ChatCompletionResponse end_resp;
      end_resp.is_stream_end = true;
      ctx->callback(end_resp);
      return total;
    }
  }

  return total;
}

}  // namespace

namespace quantclaw {

export class AnthropicProvider : public LLMProvider {
 public:
  AnthropicProvider(const std::string& api_key, const std::string& base_url,
                    int timeout, std::shared_ptr<spdlog::logger> logger);

  ChatCompletionResponse ChatCompletion(
      const ChatCompletionRequest& request) override;
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

AnthropicProvider::AnthropicProvider(const std::string& api_key,
                                     const std::string& base_url, int timeout,
                                     std::shared_ptr<spdlog::logger> logger)
    : api_key_(api_key),
      base_url_(base_url),
      timeout_(timeout),
      logger_(std::move(logger)) {
  if (base_url_.empty()) {
    base_url_ = "https://api.anthropic.com";
  }
  if (logger_) {
    logger_->info("AnthropicProvider initialized with base_url: {}",
                  base_url_);
  }
}

CurlSlist AnthropicProvider::CreateHeaders() const {
  CurlSlist headers;
  headers.append("content-type: application/json");
  headers.append("anthropic-version: 2023-06-01");
  std::string api_key_header = "x-api-key: " + api_key_;
  headers.append(api_key_header.c_str());
  return headers;
}

ChatCompletionResponse AnthropicProvider::ChatCompletion(
    const ChatCompletionRequest& request) {
  auto [system_prompt, messages_json] =
      SerializeMessagesToAnthropic(request.messages);

  nlohmann::json payload;
  payload["model"] = request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;
  payload["messages"] = messages_json;

  if (!system_prompt.empty()) {
    payload["system"] = system_prompt;
  }

  if (!request.tools.empty()) {
    payload["tools"] = ConvertToolsToAnthropic(request.tools);
    if (request.tool_choice_auto) {
      payload["tool_choice"] = {{"type", "auto"}};
    }
  }

  ApplyThinkingParams(payload, request);

  std::string json_payload = payload.dump();
  if (logger_) {
    logger_->debug("Sending request to Anthropic API: {}", json_payload);
  }

  std::string response = MakeApiRequest(json_payload);
  if (logger_) {
    logger_->debug("Received response from Anthropic API: {}", response);
  }

  nlohmann::json response_json = nlohmann::json::parse(response);

  ChatCompletionResponse result;
  if (response_json.contains("content") && response_json["content"].is_array()) {
    for (const auto& block : response_json["content"]) {
      std::string block_type = block.value("type", "");
      if (block_type == "text") {
        if (!result.content.empty())
          result.content += "\n";
        result.content += block.value("text", "");
      } else if (block_type == "thinking") {
        std::string thinking = block.value("thinking", "");
        if (thinking.empty()) {
          thinking = block.value("text", "");
        }
        if (!thinking.empty()) {
          if (!result.content.empty())
            result.content += "\n";
          result.content += thinking;
        }
      } else if (block_type == "tool_use") {
        ToolCall tc;
        tc.id = block.value("id", "");
        tc.name = block.value("name", "");
        tc.arguments = block.value("input", nlohmann::json::object());
        result.tool_calls.push_back(tc);
      }
    }
  }

  std::string stop_reason = response_json.value("stop_reason", "");
  if (stop_reason == "end_turn") {
    result.finish_reason = "stop";
  } else if (stop_reason == "tool_use") {
    result.finish_reason = "tool_calls";
  } else if (stop_reason == "max_tokens") {
    result.finish_reason = "length";
  } else {
    result.finish_reason = stop_reason;
  }

  return result;
}

std::string AnthropicProvider::GetProviderName() const {
  return "anthropic";
}

std::string AnthropicProvider::MakeApiRequest(
    const std::string& json_payload) const {
  std::string read_buffer;
  RetryAfterCapture retry_capture;

  CurlHandle curl;
  CurlSlist headers = CreateHeaders();

  std::string url = base_url_ + "/v1/messages";
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &retry_capture);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    ProviderErrorKind kind = ProviderErrorKind::kUnknown;
    if (res == CURLE_OPERATION_TIMEDOUT) {
      kind = ProviderErrorKind::kTimeout;
    } else if (res == CURLE_COULDNT_CONNECT ||
               res == CURLE_COULDNT_RESOLVE_HOST) {
      kind = ProviderErrorKind::kTransient;
    }
    throw ProviderError(
        kind, 0, "CURL request failed: " + std::string(curl_easy_strerror(res)),
        "anthropic");
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 400) {
    auto error_kind = ClassifyHttpError(static_cast<int>(http_code), read_buffer);
    if (retry_capture.retry_after_seconds > 0) {
      if (logger_) {
        logger_->warn("Anthropic API HTTP {}: rate limited, retry-after={}s",
                      http_code, retry_capture.retry_after_seconds);
      }
    } else if (logger_) {
      logger_->error("Anthropic API HTTP {}: {}", http_code,
                     read_buffer.substr(0, 256));
    }
    ProviderError err(error_kind, static_cast<int>(http_code),
                      "Anthropic API error (HTTP " + std::to_string(http_code) +
                          "): " + read_buffer,
                      "anthropic");
    err.SetRetryAfterSeconds(retry_capture.retry_after_seconds);
    throw err;
  }

  return read_buffer;
}

void AnthropicProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  auto [system_prompt, messages_json] =
      SerializeMessagesToAnthropic(request.messages);

  nlohmann::json payload;
  payload["model"] = request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;
  payload["stream"] = true;
  payload["messages"] = messages_json;

  if (!system_prompt.empty()) {
    payload["system"] = system_prompt;
  }

  if (!request.tools.empty()) {
    payload["tools"] = ConvertToolsToAnthropic(request.tools);
    if (request.tool_choice_auto) {
      payload["tool_choice"] = {{"type", "auto"}};
    }
  }

  ApplyThinkingParams(payload, request);

  std::string json_payload = payload.dump();
  if (logger_) {
    logger_->debug("Sending streaming request to Anthropic API");
  }

  AnthropicStreamContext stream_ctx;
  stream_ctx.callback = std::move(callback);
  stream_ctx.logger = logger_;

  RetryAfterCapture retry_capture;

  CurlHandle curl;
  CurlSlist headers = CreateHeaders();

  std::string url = base_url_ + "/v1/messages";
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AnthropicStreamWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &retry_capture);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    ProviderErrorKind kind = ProviderErrorKind::kUnknown;
    if (res == CURLE_OPERATION_TIMEDOUT) {
      kind = ProviderErrorKind::kTimeout;
    } else if (res == CURLE_COULDNT_CONNECT ||
               res == CURLE_COULDNT_RESOLVE_HOST) {
      kind = ProviderErrorKind::kTransient;
    }
    throw ProviderError(kind, 0,
                        "CURL streaming request failed: " +
                            std::string(curl_easy_strerror(res)),
                        "anthropic");
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 400) {
    auto error_kind = ClassifyHttpError(static_cast<int>(http_code), "");
    if (retry_capture.retry_after_seconds > 0) {
      if (logger_) {
        logger_->warn(
            "Anthropic streaming HTTP {}: rate limited, retry-after={}s",
            http_code, retry_capture.retry_after_seconds);
      }
    } else if (logger_) {
      logger_->error("Anthropic streaming HTTP {}", http_code);
    }
    ProviderError err(error_kind, static_cast<int>(http_code),
                      "Anthropic streaming API error (HTTP " +
                          std::to_string(http_code) + ")",
                      "anthropic");
    err.SetRetryAfterSeconds(retry_capture.retry_after_seconds);
    throw err;
  }
}

std::vector<std::string> AnthropicProvider::GetSupportedModels() const {
  return {"claude-sonnet-4-6", "claude-opus-4-6", "claude-haiku-4-5"};
}

}  // namespace quantclaw
