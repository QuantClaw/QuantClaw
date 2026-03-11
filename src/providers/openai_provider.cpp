// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/openai_provider.hpp"

#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/providers/provider_error.hpp"

namespace quantclaw {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                            std::string* userp) {
  userp->append((char*)contents, size * nmemb);
  return size * nmemb;
}

// Captures the Retry-After header value from HTTP response headers.
struct RetryAfterCapture {
  int retry_after_seconds = 0;
};

static size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                             void* userdata) {
  size_t total = size * nitems;
  auto* capture = static_cast<RetryAfterCapture*>(userdata);
  std::string header(buffer, total);

  // Case-insensitive prefix match for "retry-after:"
  if (header.size() > 12) {
    std::string lower = header.substr(0, 12);
    for (auto& c : lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "retry-after:") {
      std::string value = header.substr(12);
      // Trim whitespace
      auto start = value.find_first_not_of(" \t");
      if (start != std::string::npos) {
        value = value.substr(start);
      }
      try {
        capture->retry_after_seconds = std::stoi(value);
      } catch (...) {
        // Non-numeric Retry-After (e.g. HTTP-date) — ignore
      }
    }
  }
  return total;
}

// Serialize Message vector to OpenAI wire format
static nlohmann::json
serialize_messages_to_openai(const std::vector<Message>& messages) {
  nlohmann::json arr = nlohmann::json::array();

  for (const auto& msg : messages) {
    // Check what kinds of content blocks are present
    bool has_tool_use = false;
    bool has_tool_result = false;
    for (const auto& b : msg.content) {
      if (b.type == "tool_use")
        has_tool_use = true;
      if (b.type == "tool_result")
        has_tool_result = true;
    }

    if (has_tool_result) {
      // Expand into separate OpenAI "tool" role messages
      for (const auto& b : msg.content) {
        if (b.type == "tool_result") {
          arr.push_back({{"role", "tool"},
                         {"tool_call_id", b.tool_use_id},
                         {"content", b.content}});
        }
      }
    } else if (has_tool_use) {
      // Assistant message with tool_calls array
      nlohmann::json j;
      j["role"] = "assistant";

      std::string text_content = msg.text();
      if (!text_content.empty()) {
        j["content"] = text_content;
      } else {
        j["content"] = nullptr;
      }

      nlohmann::json tool_calls = nlohmann::json::array();
      for (const auto& b : msg.content) {
        if (b.type == "tool_use") {
          tool_calls.push_back(
              {{"id", b.id},
               {"type", "function"},
               {"function",
                {{"name", b.name}, {"arguments", b.input.dump()}}}});
        }
      }
      j["tool_calls"] = tool_calls;
      arr.push_back(j);
    } else {
      // Plain text message (system, user, or assistant)
      arr.push_back({{"role", msg.role}, {"content", msg.text()}});
    }
  }

  return arr;
}

OpenAIProvider::OpenAIProvider(const std::string& api_key,
                               const std::string& base_url, int timeout,
                               std::shared_ptr<spdlog::logger> logger)
    : api_key_(api_key),
      base_url_(base_url),
      timeout_(timeout),
      logger_(logger) {
  if (base_url_.empty()) {
    base_url_ = "https://api.openai.com/v1";
  }

  logger_->info("OpenAIProvider initialized with base_url: {}", base_url_);
}

ChatCompletionResponse
OpenAIProvider::ChatCompletion(const ChatCompletionRequest& request) {
  // Build JSON payload
  nlohmann::json payload;
  payload["model"] = request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;

  // Add messages
  payload["messages"] = serialize_messages_to_openai(request.messages);

  // Add tools if provided
  if (!request.tools.empty()) {
    payload["tools"] = request.tools;
    if (request.tool_choice_auto) {
      payload["tool_choice"] = "auto";
    }
  }

  std::string json_payload = payload.dump();
  logger_->debug("Sending request to OpenAI API: {}", json_payload);

  std::string response = MakeApiRequest(json_payload);
  logger_->debug("Received response from OpenAI API: {}", response);

  // Parse response
  nlohmann::json response_json = nlohmann::json::parse(response);

  ChatCompletionResponse result;
  if (response_json.contains("choices") && !response_json["choices"].empty()) {
    auto choice = response_json["choices"][0];
    if (choice.contains("message")) {
      auto message = choice["message"];
      if (message.contains("content")) {
        result.content = message["content"].get<std::string>();
      }
      if (message.contains("tool_calls")) {
        for (const auto& tc : message["tool_calls"]) {
          ToolCall tool_call;
          tool_call.id = tc.value("id", "");
          if (tc.contains("function")) {
            tool_call.name = tc["function"].value("name", "");
            if (tc["function"].contains("arguments")) {
              auto args_str = tc["function"]["arguments"].get<std::string>();
              tool_call.arguments = nlohmann::json::parse(args_str);
            }
          }
          result.tool_calls.push_back(tool_call);
        }
      }
    }
    if (choice.contains("finish_reason")) {
      result.finish_reason = choice["finish_reason"].get<std::string>();
    }
  }

  return result;
}

std::string OpenAIProvider::GetProviderName() const {
  return "openai";
}

std::string
OpenAIProvider::MakeApiRequest(const std::string& json_payload) const {
  std::string read_buffer;
  RetryAfterCapture retry_capture;

  CurlHandle curl;
  CurlSlist headers = CreateHeaders();

  std::string url = base_url_ + "/chat/completions";
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
    // Map CURL errors to ProviderError
    ProviderErrorKind kind = ProviderErrorKind::kUnknown;
    if (res == CURLE_OPERATION_TIMEDOUT) {
      kind = ProviderErrorKind::kTimeout;
    } else if (res == CURLE_COULDNT_CONNECT ||
               res == CURLE_COULDNT_RESOLVE_HOST) {
      kind = ProviderErrorKind::kTransient;
    }
    throw ProviderError(
        kind, 0, "CURL request failed: " + std::string(curl_easy_strerror(res)),
        "openai");
  }

  // Check HTTP status code
  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 400) {
    auto error_kind =
        ClassifyHttpError(static_cast<int>(http_code), read_buffer);
    if (retry_capture.retry_after_seconds > 0) {
      logger_->warn("OpenAI API HTTP {}: rate limited, retry-after={}s",
                    http_code, retry_capture.retry_after_seconds);
    } else {
      logger_->error("OpenAI API HTTP {}: {}", http_code,
                     read_buffer.substr(0, 256));
    }
    ProviderError err(error_kind, static_cast<int>(http_code),
                      "OpenAI API error (HTTP " + std::to_string(http_code) +
                          "): " + read_buffer,
                      "openai");
    err.SetRetryAfterSeconds(retry_capture.retry_after_seconds);
    throw err;
  }

  return read_buffer;
}

CurlSlist OpenAIProvider::CreateHeaders() const {
  CurlSlist headers;
  headers.append("Content-Type: application/json");

  std::string auth_header = "Authorization: Bearer " + api_key_;
  headers.append(auth_header.c_str());

  return headers;
}

// --- SSE Streaming support ---

struct StreamContext {
  std::function<void(const ChatCompletionResponse&)> callback;
  std::string buffer;  // incomplete line across chunks
  std::shared_ptr<spdlog::logger> logger;

  // Accumulated tool calls (SSE delivers them in fragments)
  struct PendingToolCall {
    std::string id;
    std::string name;
    std::string arguments;
  };
  std::vector<PendingToolCall> pending_tool_calls;
};

static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb,
                                  void* userp) {
  auto* ctx = static_cast<StreamContext*>(userp);
  size_t total = size * nmemb;
  std::string chunk(static_cast<char*>(contents), total);
  ctx->buffer += chunk;

  // Process complete lines
  size_t pos;
  while ((pos = ctx->buffer.find('\n')) != std::string::npos) {
    std::string line = ctx->buffer.substr(0, pos);
    ctx->buffer.erase(0, pos + 1);

    // Strip trailing \r
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty())
      continue;
    if (line.substr(0, 6) != "data: ")
      continue;

    std::string data = line.substr(6);
    if (data == "[DONE]") {
      // Emit any accumulated tool calls before stream end
      if (!ctx->pending_tool_calls.empty()) {
        ChatCompletionResponse tc_resp;
        tc_resp.finish_reason = "tool_calls";
        for (const auto& ptc : ctx->pending_tool_calls) {
          ToolCall tc;
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

      ChatCompletionResponse end_resp;
      end_resp.is_stream_end = true;
      ctx->callback(end_resp);
      return total;
    }

    auto j = nlohmann::json::parse(data, nullptr, false);
    if (j.is_discarded())
      continue;

    if (!j.contains("choices") || j["choices"].empty())
      continue;
    const auto& choice = j["choices"][0];
    const auto& delta = choice.value("delta", nlohmann::json::object());
    std::string finish_reason = choice.value("finish_reason", "");

    // Handle text content delta
    if (delta.contains("content") && !delta["content"].is_null()) {
      ChatCompletionResponse resp;
      resp.content = delta["content"].get<std::string>();
      ctx->callback(resp);
    }

    // Handle tool call deltas (accumulated across chunks)
    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
      for (const auto& tc_delta : delta["tool_calls"]) {
        int index = tc_delta.value("index", 0);

        // Ensure vector is large enough
        while (static_cast<int>(ctx->pending_tool_calls.size()) <= index) {
          ctx->pending_tool_calls.push_back({});
        }

        auto& ptc = ctx->pending_tool_calls[index];
        if (tc_delta.contains("id") && !tc_delta["id"].is_null()) {
          ptc.id = tc_delta["id"].get<std::string>();
        }
        if (tc_delta.contains("function")) {
          const auto& fn = tc_delta["function"];
          if (fn.contains("name") && !fn["name"].is_null()) {
            ptc.name = fn["name"].get<std::string>();
          }
          if (fn.contains("arguments") && !fn["arguments"].is_null()) {
            ptc.arguments += fn["arguments"].get<std::string>();
          }
        }
      }
    }

    // If finish_reason is "tool_calls", emit accumulated tool calls now
    if (finish_reason == "tool_calls" && !ctx->pending_tool_calls.empty()) {
      ChatCompletionResponse tc_resp;
      tc_resp.finish_reason = "tool_calls";
      for (const auto& ptc : ctx->pending_tool_calls) {
        ToolCall tc;
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
  }

  return total;
}

void OpenAIProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  // Build JSON payload with stream=true
  nlohmann::json payload;
  payload["model"] = request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;
  payload["stream"] = true;

  payload["messages"] = serialize_messages_to_openai(request.messages);

  if (!request.tools.empty()) {
    payload["tools"] = request.tools;
    if (request.tool_choice_auto) {
      payload["tool_choice"] = "auto";
    }
  }

  std::string json_payload = payload.dump();
  logger_->debug("Sending streaming request to OpenAI API");

  StreamContext stream_ctx;
  stream_ctx.callback = callback;
  stream_ctx.logger = logger_;

  RetryAfterCapture retry_capture;

  CurlHandle curl;
  CurlSlist headers = CreateHeaders();

  std::string url = base_url_ + "/chat/completions";
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &retry_capture);

  // For streaming: no hard timeout, use low-speed detection instead
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
                        "openai");
  }

  // Check HTTP status code for streaming requests too
  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 400) {
    auto error_kind = ClassifyHttpError(static_cast<int>(http_code), "");
    if (retry_capture.retry_after_seconds > 0) {
      logger_->warn("OpenAI streaming HTTP {}: rate limited, retry-after={}s",
                    http_code, retry_capture.retry_after_seconds);
    } else {
      logger_->error("OpenAI streaming HTTP {}", http_code);
    }
    ProviderError err(error_kind, static_cast<int>(http_code),
                      "OpenAI streaming API error (HTTP " +
                          std::to_string(http_code) + ")",
                      "openai");
    err.SetRetryAfterSeconds(retry_capture.retry_after_seconds);
    throw err;
  }
}

std::vector<std::string> OpenAIProvider::GetSupportedModels() const {
  return {"gpt-4-turbo", "gpt-4", "gpt-3.5-turbo"};
}

}  // namespace quantclaw