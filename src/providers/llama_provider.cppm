// Copyright 2026 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0
//
// LlamaProvider — LLMProvider implementation that speaks the
// OpenAI-compatible /v1/chat/completions API exposed by llama-server
// (from llama.cpp).  This is the backing implementation for the "local"
// provider factory in ProviderRegistry.
//
// It uses libcurl + server-sent event (SSE) parsing — same technique as
// AnthropicProvider — so no llama.cpp C++ headers are required at compile
// time.  The server binary (llama-server) is built as a separate CMake
// sub-project and launched externally.

module;
#include <cctype>

#include <curl/curl.h>
#include <spdlog/spdlog.h>

export module quantclaw.providers.llama_provider;

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

// Serialize QuantClaw messages → OpenAI-format JSON array.
// tool_result blocks are promoted to standalone "tool" role messages so that
// llama-server's function-calling pipeline can consume them correctly.
nlohmann::json
SerializeMessages(const std::vector<quantclaw::Message>& messages) {
  nlohmann::json arr = nlohmann::json::array();

  for (const auto& msg : messages) {
    bool has_tool_use = false;
    bool has_tool_result = false;
    for (const auto& b : msg.content) {
      if (b.type == "tool_use")
        has_tool_use = true;
      if (b.type == "tool_result")
        has_tool_result = true;
    }

    if (has_tool_result) {
      // Each tool_result block → separate "tool" role entry.
      for (const auto& b : msg.content) {
        if (b.type != "tool_result")
          continue;
        // b.content is already a std::string
        arr.push_back({{"role", "tool"},
                       {"tool_call_id", b.tool_use_id},
                       {"content", b.content}});
      }
      continue;
    }

    nlohmann::json entry;
    entry["role"] = msg.role;

    if (has_tool_use) {
      entry["content"] = nullptr;
      nlohmann::json tool_calls = nlohmann::json::array();
      for (const auto& b : msg.content) {
        if (b.type != "tool_use")
          continue;
        tool_calls.push_back(
            {{"id", b.id},
             {"type", "function"},
             {"function", {{"name", b.name}, {"arguments", b.input.dump()}}}});
      }
      entry["tool_calls"] = tool_calls;
    } else {
      std::string text;
      for (const auto& b : msg.content) {
        if (b.type == "text" || b.type == "thinking")
          text += b.text;
      }
      entry["content"] = text;
    }

    arr.push_back(entry);
  }

  return arr;
}

// Translate QuantClaw tool definitions → OpenAI function-calling schema.
nlohmann::json ConvertTools(const std::vector<nlohmann::json>& qc_tools) {
  nlohmann::json out = nlohmann::json::array();
  for (const auto& t : qc_tools) {
    out.push_back({{"type", "function"}, {"function", t}});
  }
  return out;
}

// Parse a completed (non-streaming) OpenAI-format response body.
quantclaw::ChatCompletionResponse ParseResponse(const std::string& body) {
  quantclaw::ChatCompletionResponse result;
  try {
    auto j = nlohmann::json::parse(body);
    const auto& choice = j.at("choices").at(0);
    const auto& msg = choice.at("message");

    result.finish_reason = choice.value("finish_reason", std::string{"stop"});

    if (msg.contains("content") && !msg["content"].is_null()) {
      result.content = msg["content"].get<std::string>();
    }

    if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
      for (const auto& tc : msg["tool_calls"]) {
        quantclaw::ToolCall call;
        call.id = tc.value("id", std::string{});
        const auto& fn = tc.at("function");
        call.name = fn.value("name", std::string{});
        std::string args_str = fn.value("arguments", std::string{"null"});
        try {
          call.arguments = nlohmann::json::parse(args_str);
        } catch (...) {
          call.arguments = args_str;
        }
        result.tool_calls.push_back(std::move(call));
      }
    }

    if (j.contains("usage")) {
      const auto& u = j["usage"];
      result.usage.prompt_tokens = u.value("prompt_tokens", 0);
      result.usage.completion_tokens = u.value("completion_tokens", 0);
      result.usage.total_tokens = u.value("total_tokens", 0);
    }
  } catch (const std::exception& e) {
    throw quantclaw::ProviderError(
        quantclaw::ProviderErrorKind::kUnknown, 0,
        std::string("LlamaProvider: failed to parse response: ") + e.what(),
        "local");
  }
  return result;
}

// ── SSE streaming ────────────────────────────────────────────────────────────

struct StreamContext {
  std::string buffer;
  std::function<void(const quantclaw::ChatCompletionResponse&)> callback;
  std::shared_ptr<spdlog::logger> logger;

  // Accumulate tool-call argument fragments across delta events.
  std::unordered_map<int, quantclaw::ToolCall> tool_accum;

  void flush_tool_calls() {
    if (tool_accum.empty())
      return;
    quantclaw::ChatCompletionResponse tc_resp;
    for (auto& [idx, tc] : tool_accum) {
      if (tc.arguments.is_string()) {
        try {
          tc.arguments = nlohmann::json::parse(tc.arguments.get<std::string>());
        } catch (...) {
          // keep as string
        }
      }
      tc_resp.tool_calls.push_back(std::move(tc));
    }
    tool_accum.clear();
    callback(tc_resp);
  }

  void process_line(const std::string& line) {
    if (line == "data: [DONE]") {
      flush_tool_calls();
      quantclaw::ChatCompletionResponse end_resp;
      end_resp.is_stream_end = true;
      callback(end_resp);
      return;
    }
    if (!line.starts_with("data: "))
      return;

    try {
      auto j = nlohmann::json::parse(line.substr(6));
      const auto& choice = j.at("choices").at(0);
      const auto& delta = choice.at("delta");

      if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tc_delta : delta["tool_calls"]) {
          int idx = tc_delta.value("index", 0);
          auto& tc = tool_accum[idx];
          if (tc_delta.contains("id") && !tc_delta["id"].is_null()) {
            tc.id = tc_delta["id"].get<std::string>();
          }
          if (tc_delta.contains("function")) {
            const auto& fn = tc_delta["function"];
            if (fn.contains("name") && !fn["name"].is_null()) {
              tc.name = fn["name"].get<std::string>();
            }
            if (fn.contains("arguments") && !fn["arguments"].is_null()) {
              std::string existing = tc.arguments.is_string()
                                         ? tc.arguments.get<std::string>()
                                         : std::string{};
              tc.arguments = existing + fn["arguments"].get<std::string>();
            }
          }
        }
        return;
      }

      if (choice.value("finish_reason", std::string{}) == "tool_calls") {
        flush_tool_calls();
        return;
      }

      if (delta.contains("content") && !delta["content"].is_null()) {
        quantclaw::ChatCompletionResponse resp;
        resp.content = delta["content"].get<std::string>();
        if (!resp.content.empty())
          callback(resp);
      }
    } catch (const std::exception& e) {
      if (logger)
        logger->debug("LlamaProvider SSE parse: {}", e.what());
    }
  }

  void push(const char* data, size_t len) {
    buffer.append(data, len);
    std::string::size_type pos;
    while ((pos = buffer.find('\n')) != std::string::npos) {
      std::string line = buffer.substr(0, pos);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      buffer.erase(0, pos + 1);
      process_line(line);
    }
  }
};

size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb,
                           void* userdata) {
  size_t total = size * nmemb;
  static_cast<StreamContext*>(userdata)->push(
      static_cast<const char*>(contents), total);
  return total;
}

}  // namespace

export namespace quantclaw {

// LlamaProvider talks to a running llama-server via its OpenAI-compatible
// HTTP API.  It is registered as the "local" provider factory so that the
// agent loop routes all inference to the local llama.cpp instance by default.
class LlamaProvider : public LLMProvider {
 public:
  LlamaProvider(std::string base_url, int timeout_seconds,
                std::shared_ptr<spdlog::logger> logger)
      : base_url_(std::move(base_url)),
        timeout_(timeout_seconds),
        logger_(std::move(logger)) {}

  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& request) override;

  void ChatCompletionStream(
      const ChatCompletionRequest& request,
      std::function<void(const ChatCompletionResponse&)> callback) override;

  std::string GetProviderName() const override {
    return "local";
  }

  // llama-server exposes whatever model it was started with.  The registry
  // can query /v1/models at runtime; we return empty here.
  std::vector<std::string> GetSupportedModels() const override {
    return {};
  }

 private:
  nlohmann::json BuildPayload(const ChatCompletionRequest& request,
                              bool stream) const;
  CurlSlist BuildHeaders() const;

  std::string base_url_;
  int timeout_;
  std::shared_ptr<spdlog::logger> logger_;
};

// ── Implementation ───────────────────────────────────────────────────────────

nlohmann::json LlamaProvider::BuildPayload(const ChatCompletionRequest& request,
                                           bool stream) const {
  nlohmann::json payload;
  payload["model"] = request.model.empty() ? "local" : request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;
  payload["stream"] = stream;
  payload["messages"] = SerializeMessages(request.messages);
  if (!request.tools.empty()) {
    payload["tools"] = ConvertTools(request.tools);
    if (request.tool_choice_auto)
      payload["tool_choice"] = "auto";
  }
  return payload;
}

CurlSlist LlamaProvider::BuildHeaders() const {
  CurlSlist headers;
  headers.append("Content-Type: application/json");
  headers.append("Accept: application/json");
  return headers;
}

ChatCompletionResponse
LlamaProvider::ChatCompletion(const ChatCompletionRequest& request) {
  const std::string json_payload = BuildPayload(request, false).dump();
  const std::string url = base_url_ + "/v1/chat/completions";
  if (logger_)
    logger_->debug("LlamaProvider: POST {}", url);

  std::string read_buffer;
  CurlHandle curl;
  CurlSlist headers = BuildHeaders();

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_));

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    ProviderErrorKind kind = ProviderErrorKind::kUnknown;
    if (res == CURLE_OPERATION_TIMEDOUT)
      kind = ProviderErrorKind::kTimeout;
    else if (res == CURLE_COULDNT_CONNECT || res == CURLE_COULDNT_RESOLVE_HOST)
      kind = ProviderErrorKind::kTransient;
    throw ProviderError(kind, 0,
                        "LlamaProvider CURL error: " +
                            std::string(curl_easy_strerror(res)),
                        "local");
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400) {
    auto kind = ClassifyHttpError(static_cast<int>(http_code), read_buffer);
    throw ProviderError(kind, static_cast<int>(http_code),
                        "llama-server HTTP " + std::to_string(http_code) +
                            ": " + read_buffer,
                        "local");
  }

  return ParseResponse(read_buffer);
}

void LlamaProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  const std::string json_payload = BuildPayload(request, true).dump();
  const std::string url = base_url_ + "/v1/chat/completions";
  if (logger_)
    logger_->debug("LlamaProvider: streaming POST {}", url);

  StreamContext stream_ctx;
  stream_ctx.callback = std::move(callback);
  stream_ctx.logger = logger_;

  CurlHandle curl;
  CurlSlist headers = BuildHeaders();

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    ProviderErrorKind kind = ProviderErrorKind::kUnknown;
    if (res == CURLE_OPERATION_TIMEDOUT)
      kind = ProviderErrorKind::kTimeout;
    else if (res == CURLE_COULDNT_CONNECT || res == CURLE_COULDNT_RESOLVE_HOST)
      kind = ProviderErrorKind::kTransient;
    throw ProviderError(kind, 0,
                        "LlamaProvider CURL streaming error: " +
                            std::string(curl_easy_strerror(res)),
                        "local");
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400) {
    auto kind = ClassifyHttpError(static_cast<int>(http_code), "");
    throw ProviderError(
        kind, static_cast<int>(http_code),
        "llama-server streaming HTTP " + std::to_string(http_code), "local");
  }
}

}  // namespace quantclaw
