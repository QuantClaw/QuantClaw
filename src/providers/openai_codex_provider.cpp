// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/openai_codex_provider.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "quantclaw/providers/provider_error.hpp"

namespace quantclaw {
namespace {

constexpr size_t kMaxErrorBodyBytes = 128 * 1024;

nlohmann::json
convert_tools_to_responses(const std::vector<nlohmann::json>& tools) {
  nlohmann::json converted = nlohmann::json::array();
  for (const auto& tool : tools) {
    if (!tool.is_object()) {
      continue;
    }
    if (tool.value("type", "") == "function" && tool.contains("function")) {
      const auto& fn = tool["function"];
      converted.push_back(
          {{"type", "function"},
           {"name", fn.value("name", "")},
           {"description", fn.value("description", "")},
           {"parameters", fn.value("parameters", nlohmann::json::object())}});
    } else {
      converted.push_back(tool);
    }
  }
  return converted;
}

nlohmann::json build_input_items(const std::vector<Message>& messages,
                                 std::string* instructions) {
  nlohmann::json input = nlohmann::json::array();

  for (const auto& msg : messages) {
    const std::string text = msg.text();
    if (msg.role == "system") {
      if (!text.empty()) {
        if (!instructions->empty()) {
          *instructions += "\n\n";
        }
        *instructions += text;
      }
      continue;
    }

    bool has_tool_use = false;
    bool has_tool_result = false;
    for (const auto& block : msg.content) {
      if (block.type == "tool_use")
        has_tool_use = true;
      if (block.type == "tool_result")
        has_tool_result = true;
    }

    if (has_tool_result) {
      for (const auto& block : msg.content) {
        if (block.type == "tool_result") {
          input.push_back({{"type", "function_call_output"},
                           {"call_id", block.tool_use_id},
                           {"output", block.content}});
        }
      }
      continue;
    }

    if (has_tool_use) {
      for (const auto& block : msg.content) {
        if (block.type == "tool_use") {
          input.push_back({{"type", "function_call"},
                           {"call_id", block.id},
                           {"name", block.name},
                           {"arguments", block.input.dump()}});
        }
      }
      if (!text.empty()) {
        input.push_back({{"role", "assistant"}, {"content", text}});
      }
      continue;
    }

    input.push_back({{"role", msg.role}, {"content", text}});
  }

  return input;
}

void populate_usage(const nlohmann::json& response_json,
                    ChatCompletionResponse* response) {
  if (!response_json.contains("usage") || !response_json["usage"].is_object()) {
    return;
  }
  const auto& usage = response_json["usage"];
  response->usage.prompt_tokens = usage.value("input_tokens", 0);
  response->usage.completion_tokens = usage.value("output_tokens", 0);
  response->usage.total_tokens = usage.value("total_tokens", 0);
}

struct StreamContext {
  std::function<void(const ChatCompletionResponse&)> callback;
  std::string buffer;
  std::string raw_body;
  std::string callback_error;
  std::unordered_map<std::string, ToolCall> pending_tool_calls;
};

void handle_stream_event(const nlohmann::json& event, StreamContext* ctx) {
  const std::string type = event.value("type", "");
  if (type.empty()) {
    return;
  }

  if (type == "error") {
    throw std::runtime_error("Codex error: " + event.dump());
  }
  if (type == "response.failed") {
    throw std::runtime_error(
        event.value("message", std::string("Codex response failed")));
  }
  if (type == "response.output_text.delta") {
    ChatCompletionResponse chunk;
    chunk.content = event.value("delta", "");
    if (!chunk.content.empty()) {
      ctx->callback(chunk);
    }
    return;
  }
  if (type == "response.output_item.added" && event.contains("item")) {
    const auto& item = event["item"];
    if (item.value("type", "") == "function_call") {
      ToolCall tool_call;
      tool_call.id = item.value("call_id", item.value("id", ""));
      tool_call.name = item.value("name", "");
      tool_call.arguments = nlohmann::json::object();
      ctx->pending_tool_calls[item.value("id", tool_call.id)] = tool_call;
    }
    return;
  }
  if (type == "response.function_call_arguments.done") {
    const std::string item_id = event.value("item_id", "");
    ToolCall tool_call;
    auto it = ctx->pending_tool_calls.find(item_id);
    if (it != ctx->pending_tool_calls.end()) {
      tool_call = it->second;
      ctx->pending_tool_calls.erase(it);
    }
    tool_call.id = event.value("call_id", tool_call.id);
    tool_call.name = event.value("name", tool_call.name);
    tool_call.arguments =
        nlohmann::json::parse(event.value("arguments", "{}"), nullptr, false);
    if (tool_call.arguments.is_discarded()) {
      tool_call.arguments = nlohmann::json::object();
    }
    ChatCompletionResponse chunk;
    chunk.finish_reason = "tool_calls";
    chunk.tool_calls.push_back(std::move(tool_call));
    ctx->callback(chunk);
    return;
  }
  if (type == "response.completed" || type == "response.done" ||
      type == "response.incomplete") {
    ChatCompletionResponse end;
    end.is_stream_end = true;
    end.finish_reason = type == "response.incomplete" ? "length" : "stop";
    if (event.contains("response") && event["response"].is_object()) {
      populate_usage(event["response"], &end);
    }
    ctx->callback(end);
  }
}

size_t stream_write_callback(void* contents, size_t size, size_t nmemb,
                             void* userp) {
  auto* ctx = static_cast<StreamContext*>(userp);
  const auto* chars = static_cast<char*>(contents);
  const size_t total = size * nmemb;
  if (ctx->raw_body.size() < kMaxErrorBodyBytes) {
    const size_t remaining = kMaxErrorBodyBytes - ctx->raw_body.size();
    ctx->raw_body.append(chars, std::min(total, remaining));
  }
  ctx->buffer.append(chars, total);

  try {
    size_t pos = 0;
    while ((pos = ctx->buffer.find("\n\n")) != std::string::npos) {
      std::string chunk = ctx->buffer.substr(0, pos);
      ctx->buffer.erase(0, pos + 2);

      std::stringstream lines(chunk);
      std::string line;
      std::string data;
      while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        if (line.rfind("data: ", 0) == 0) {
          data += line.substr(6);
        }
      }
      if (data.empty() || data == "[DONE]") {
        continue;
      }
      auto event = nlohmann::json::parse(data, nullptr, false);
      if (event.is_discarded()) {
        continue;
      }
      handle_stream_event(event, ctx);
    }
  } catch (const std::exception& e) {
    ctx->callback_error = e.what();
    return 0;
  }

  return size * nmemb;
}

nlohmann::json build_codex_payload(const ChatCompletionRequest& request) {
  nlohmann::json payload;
  std::string instructions;

  payload["model"] = request.model;
  payload["store"] = false;
  payload["stream"] = true;
  payload["input"] = build_input_items(request.messages, &instructions);
  payload["instructions"] = instructions;

  if (!request.tools.empty()) {
    payload["tools"] = convert_tools_to_responses(request.tools);
  }

  if (request.max_tokens > 0) {
    payload["max_output_tokens"] = request.max_tokens;
  }

  return payload;
}

}  // namespace

OpenAICodexProvider::OpenAICodexProvider(
    const std::string& base_url, int timeout,
    std::shared_ptr<spdlog::logger> logger,
    std::shared_ptr<auth::BearerTokenSource> token_source)
    : base_url_(base_url.empty() ? "https://chatgpt.com/backend-api"
                                 : base_url),
      timeout_(timeout),
      logger_(std::move(logger)),
      token_source_(std::move(token_source)) {}

ChatCompletionResponse
OpenAICodexProvider::ChatCompletion(const ChatCompletionRequest& request) {
  ChatCompletionResponse aggregated;
  StreamContext ctx;
  ctx.callback = [&](const ChatCompletionResponse& chunk) {
    aggregated.content += chunk.content;
    aggregated.tool_calls.insert(aggregated.tool_calls.end(),
                                 chunk.tool_calls.begin(),
                                 chunk.tool_calls.end());
    if (chunk.is_stream_end) {
      aggregated.is_stream_end = true;
      aggregated.finish_reason = chunk.finish_reason;
      aggregated.usage = chunk.usage;
    } else if (!chunk.finish_reason.empty()) {
      aggregated.finish_reason = chunk.finish_reason;
    }
  };

  CurlHandle curl;
  auto headers = CreateHeaders(token_source_->ResolveAccessToken());
  const std::string endpoint = ResolveEndpoint();
  const std::string body = build_codex_payload(request).dump();

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);

  CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    throw ProviderError(ProviderErrorKind::kUnknown, 0,
                        "OpenAI Codex request failed: " +
                            std::string(curl_easy_strerror(code)),
                        "openai-codex");
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400) {
    throw ProviderError(
        ClassifyHttpError(static_cast<int>(http_code), ctx.raw_body),
        static_cast<int>(http_code), ctx.raw_body, "openai-codex");
  }
  if (!ctx.callback_error.empty()) {
    throw ProviderError(ProviderErrorKind::kUnknown, 0, ctx.callback_error,
                        "openai-codex");
  }

  if (aggregated.finish_reason.empty()) {
    aggregated.finish_reason =
        aggregated.tool_calls.empty() ? "stop" : "tool_calls";
  }
  return aggregated;
}

void OpenAICodexProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  StreamContext ctx;
  ctx.callback = std::move(callback);

  CurlHandle curl;
  auto headers = CreateHeaders(token_source_->ResolveAccessToken());
  const std::string endpoint = ResolveEndpoint();
  const std::string body = build_codex_payload(request).dump();

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);

  CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    throw ProviderError(ProviderErrorKind::kUnknown, 0,
                        "OpenAI Codex streaming request failed: " +
                            std::string(curl_easy_strerror(code)),
                        "openai-codex");
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400) {
    throw ProviderError(
        ClassifyHttpError(static_cast<int>(http_code), ctx.raw_body),
        static_cast<int>(http_code), ctx.raw_body, "openai-codex");
  }
  if (!ctx.callback_error.empty()) {
    throw ProviderError(ProviderErrorKind::kUnknown, 0, ctx.callback_error,
                        "openai-codex");
  }
}

std::string OpenAICodexProvider::GetProviderName() const {
  return "openai-codex";
}

std::vector<std::string> OpenAICodexProvider::GetSupportedModels() const {
  return {"gpt-5", "gpt-5-codex", "gpt-5-mini"};
}

std::string OpenAICodexProvider::ResolveEndpoint() const {
  std::string normalized = base_url_;
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  if (normalized.size() >= 16 &&
      normalized.rfind("/codex/responses") == normalized.size() - 16) {
    return normalized;
  }
  if (normalized.size() >= 6 &&
      normalized.rfind("/codex") == normalized.size() - 6) {
    return normalized + "/responses";
  }
  return normalized + "/codex/responses";
}

CurlSlist
OpenAICodexProvider::CreateHeaders(const std::string& bearer_token) const {
  CurlSlist headers;
  headers.append("Content-Type: application/json");
  headers.append("Accept: application/json, text/event-stream");
  const std::string auth_header = "Authorization: Bearer " + bearer_token;
  headers.append(auth_header.c_str());
  return headers;
}

}  // namespace quantclaw
