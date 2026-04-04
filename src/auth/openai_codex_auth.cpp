// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/auth/openai_codex_auth.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <curl/curl.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <shellapi.h>
#endif
#include "quantclaw/providers/curl_raii.hpp"
#include "quantclaw/providers/provider_error.hpp"

#include <openssl/evp.h>
#include <openssl/sha.h>

namespace quantclaw::auth {
namespace {

constexpr char kClientId[] = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr char kAuthorizeUrl[] = "https://auth.openai.com/oauth/authorize";
constexpr char kTokenUrl[] = "https://auth.openai.com/oauth/token";
constexpr char kDefaultRedirectUri[] = "http://localhost:1455/auth/callback";
constexpr char kScope[] = "openid profile email offline_access";

size_t write_callback(void* contents, size_t size, size_t nmemb,
                      std::string* out) {
  out->append(static_cast<char*>(contents), size * nmemb);
  return size * nmemb;
}

std::int64_t now_epoch_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string url_encode(std::string_view value) {
  std::ostringstream out;
  out << std::hex << std::uppercase;
  for (char raw_ch : value) {
    const auto ch = static_cast<unsigned char>(raw_ch);
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~') {
      out << ch;
    } else {
      out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return out.str();
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  return -1;
}

std::string url_decode(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  size_t i = 0;
  while (i < value.size()) {
    const char ch = value[i];
    if (ch == '+') {
      out.push_back(' ');
      ++i;
      continue;
    }
    if (ch == '%' && i + 2 < value.size()) {
      const int hi = hex_value(value[i + 1]);
      const int lo = hex_value(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 3;
        continue;
      }
    }
    out.push_back(ch);
    ++i;
  }
  return out;
}

// Like url_decode but treats '+' as a literal '+', not a space.
// Use for opaque auth codes that must not have query-string semantics applied.
std::string url_decode_percent_only(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  size_t i = 0;
  while (i < value.size()) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const int hi = hex_value(value[i + 1]);
      const int lo = hex_value(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 3;
        continue;
      }
    }
    out.push_back(value[i]);
    ++i;
  }
  return out;
}

std::string base64url_encode(const unsigned char* data, size_t len) {
  std::string encoded(4 * ((len + 2) / 3), '\0');
  int out_len =
      EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()), data,
                      static_cast<int>(len));
  encoded.resize(static_cast<size_t>(out_len));
  for (char& ch : encoded) {
    if (ch == '+')
      ch = '-';
    else if (ch == '/')
      ch = '_';
  }
  while (!encoded.empty() && encoded.back() == '=') {
    encoded.pop_back();
  }
  return encoded;
}

std::string base64url_decode(std::string value) {
  for (char& ch : value) {
    if (ch == '-')
      ch = '+';
    else if (ch == '_')
      ch = '/';
  }
  while (value.size() % 4 != 0) {
    value.push_back('=');
  }

  std::string decoded((value.size() / 4) * 3, '\0');
  int out_len =
      EVP_DecodeBlock(reinterpret_cast<unsigned char*>(decoded.data()),
                      reinterpret_cast<const unsigned char*>(value.data()),
                      static_cast<int>(value.size()));
  if (out_len < 0) {
    return {};
  }
  size_t padding = 0;
  if (!value.empty() && value.back() == '=')
    ++padding;
  if (value.size() > 1 && value[value.size() - 2] == '=')
    ++padding;
  decoded.resize(static_cast<size_t>(out_len) - padding);
  return decoded;
}

std::string random_urlsafe_string(size_t bytes = 32) {
  std::random_device rd;
  std::uniform_int_distribution<int> dist(0, 255);
  std::string raw(bytes, '\0');
  for (char& ch : raw) {
    ch = static_cast<char>(dist(rd));
  }
  return base64url_encode(reinterpret_cast<const unsigned char*>(raw.data()),
                          raw.size());
}

std::string sha256_base64url(std::string_view input) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(),
         digest.data());
  return base64url_encode(digest.data(), digest.size());
}

std::optional<nlohmann::json> parse_jwt_payload(std::string_view token) {
  const size_t first = token.find('.');
  if (first == std::string_view::npos)
    return std::nullopt;
  const size_t second = token.find('.', first + 1);
  if (second == std::string_view::npos)
    return std::nullopt;

  std::string payload = base64url_decode(
      std::string(token.substr(first + 1, second - first - 1)));
  if (payload.empty()) {
    return std::nullopt;
  }

  auto json = nlohmann::json::parse(payload, nullptr, false);
  if (json.is_discarded()) {
    return std::nullopt;
  }
  return json;
}

std::string extract_email_from_id_token(const nlohmann::json& token_json) {
  if (!token_json.contains("id_token") || !token_json["id_token"].is_string()) {
    return "";
  }
  auto payload = parse_jwt_payload(token_json["id_token"].get<std::string>());
  if (!payload || !payload->contains("email") ||
      !(*payload)["email"].is_string()) {
    return "";
  }
  return (*payload)["email"].get<std::string>();
}

std::string post_form(const std::string& url, const std::string& body,
                      int timeout_seconds) {
  std::string response_body;
  CurlHandle curl;
  CurlSlist headers;
  headers.append("Content-Type: application/x-www-form-urlencoded");
  headers.append("Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);

  CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    throw std::runtime_error("OAuth token request failed: " +
                             std::string(curl_easy_strerror(code)));
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400) {
    throw std::runtime_error("OAuth token endpoint returned HTTP " +
                             std::to_string(http_code) + ": " + response_body);
  }
  return response_body;
}

OpenAICodexAuthRecord token_record_from_json(const nlohmann::json& token_json) {
  OpenAICodexAuthRecord record;
  record.provider = "openai-codex";
  record.access_token = token_json.value("access_token", "");
  record.refresh_token = token_json.value("refresh_token", "");
  record.token_type = token_json.value("token_type", "Bearer");
  record.scope = token_json.value("scope", "");
  record.expires_at = now_epoch_seconds() + token_json.value("expires_in", 0);
  record.account_id =
      OpenAICodexOAuthClient::ExtractAccountId(record.access_token);
  record.email = extract_email_from_id_token(token_json);
  return record;
}

std::string html_response(const std::string& title, const std::string& body) {
  std::ostringstream out;
  out << "<!doctype html><html><head><meta charset='utf-8'><title>" << title
      << "</title></head><body><h1>" << title << "</h1><p>" << body
      << "</p></body></html>";
  return out.str();
}

bool open_browser(const std::string& url) {
#ifdef _WIN32
  return reinterpret_cast<intptr_t>(ShellExecuteA(nullptr, "open", url.c_str(),
                                                  nullptr, nullptr,
                                                  SW_SHOWNORMAL)) > 32;
#elif defined(__APPLE__)
  std::string cmd = "open \"" + url + "\"";
  return std::system(cmd.c_str()) == 0;
#else
  std::string cmd = "xdg-open \"" + url + "\"";
  return std::system(cmd.c_str()) == 0;
#endif
}

std::string extract_query_param(const std::string& query,
                                std::string_view key) {
  const std::string prefix = std::string(key) + "=";
  size_t start = 0;
  while (start <= query.size()) {
    const size_t end = query.find('&', start);
    const std::string_view part(
        query.data() + start,
        (end == std::string::npos ? query.size() : end) - start);
    if (part.rfind(prefix, 0) == 0) {
      return std::string(part.substr(prefix.size()));
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return "";
}

std::string parse_manual_code(std::string input) {
  if (input.find("code=") == std::string::npos) {
    // Raw auth code: decode percent-encoded sequences but treat '+' literally,
    // since auth codes are opaque tokens and '+' is not a space placeholder.
    return url_decode_percent_only(input);
  }
  auto pos = input.find('?');
  std::string query = pos == std::string::npos ? input : input.substr(pos + 1);
  return url_decode(extract_query_param(query, "code"));
}

}  // namespace

std::string ParseOpenAICodexManualCode(std::string input) {
  return parse_manual_code(std::move(input));
}
OpenAICodexAuthStore::OpenAICodexAuthStore(std::filesystem::path path)
    : ProviderAuthStore(std::move(path)) {}

std::filesystem::path OpenAICodexAuthStore::DefaultPath() {
  return DefaultPathFor("openai-codex");
}

std::string BuildOpenAICodexAuthorizeUrl(const std::string& state,
                                         const std::string& code_challenge,
                                         const std::string& redirect_uri) {
  std::ostringstream out;
  out << kAuthorizeUrl << "?response_type=code"
      << "&client_id=" << url_encode(kClientId)
      << "&redirect_uri=" << url_encode(redirect_uri)
      << "&scope=" << url_encode(kScope)
      << "&code_challenge=" << url_encode(code_challenge)
      << "&code_challenge_method=S256" << "&state=" << url_encode(state)
      << "&id_token_add_organizations=true" << "&codex_cli_simplified_flow=true"
      << "&originator=pi";
  return out.str();
}

std::optional<OpenAICodexCallbackBindTarget>
ParseOpenAICodexCallbackBindTarget(std::string_view redirect_uri) {
  const size_t scheme_sep = redirect_uri.find("://");
  if (scheme_sep == std::string_view::npos) {
    return std::nullopt;
  }

  const size_t authority_start = scheme_sep + 3;
  const size_t path_start = redirect_uri.find('/', authority_start);
  std::string_view authority =
      redirect_uri.substr(authority_start, path_start - authority_start);
  if (authority.empty()) {
    return std::nullopt;
  }

  std::string host;
  int port = 0;
  if (authority.front() == '[') {
    const size_t bracket_end = authority.find(']');
    if (bracket_end == std::string_view::npos) {
      return std::nullopt;
    }
    host = std::string(authority.substr(1, bracket_end - 1));
    if (bracket_end + 1 < authority.size()) {
      if (authority[bracket_end + 1] != ':') {
        return std::nullopt;
      }
      auto port_view = authority.substr(bracket_end + 2);
      if (port_view.empty()) {
        return std::nullopt;
      }
      try {
        port = std::stoi(std::string(port_view));
      } catch (const std::exception&) {
        return std::nullopt;
      }
    }
  } else {
    const size_t colon = authority.rfind(':');
    if (colon == std::string_view::npos) {
      host = std::string(authority);
    } else {
      host = std::string(authority.substr(0, colon));
      auto port_view = authority.substr(colon + 1);
      if (port_view.empty()) {
        return std::nullopt;
      }
      try {
        port = std::stoi(std::string(port_view));
      } catch (const std::exception&) {
        return std::nullopt;
      }
    }
  }

  if (host.empty()) {
    return std::nullopt;
  }

  if (port == 0) {
    const auto scheme = redirect_uri.substr(0, scheme_sep);
    if (scheme == "http") {
      port = 80;
    } else if (scheme == "https") {
      port = 443;
    } else {
      return std::nullopt;
    }
  }

  if (port <= 0 || port > 65535) {
    return std::nullopt;
  }

  return OpenAICodexCallbackBindTarget{std::move(host), port};
}

OpenAICodexOAuthClient::OpenAICodexOAuthClient(
    std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

OpenAICodexAuthRecord
OpenAICodexOAuthClient::LoginInteractive(std::istream& in, std::ostream& out) {
  const std::string redirect_uri = kDefaultRedirectUri;
  const auto bind_target =
      ParseOpenAICodexCallbackBindTarget(redirect_uri)
          .value_or(OpenAICodexCallbackBindTarget{"127.0.0.1", 1455});
  const std::string state = random_urlsafe_string(24);
  const std::string code_verifier = random_urlsafe_string(48);
  const std::string code_challenge = sha256_base64url(code_verifier);
  const std::string auth_url =
      BuildOpenAICodexAuthorizeUrl(state, code_challenge, redirect_uri);

  struct CallbackState {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
    std::string code;
    std::string error;
  } callback_state;

  httplib::Server server;
  server.Get("/auth/callback", [&](const httplib::Request& req,
                                   httplib::Response& res) {
    std::lock_guard<std::mutex> lock(callback_state.mu);
    const std::string req_state = req.get_param_value("state");
    if (req_state != state) {
      callback_state.done = true;
      callback_state.ok = false;
      callback_state.error = "OAuth state mismatch";
      res.set_content(html_response("QuantClaw login failed",
                                    "State validation failed. You can close "
                                    "this window and retry."),
                      "text/html");
    } else if (req.has_param("error")) {
      callback_state.done = true;
      callback_state.ok = false;
      callback_state.error = req.get_param_value("error");
      res.set_content(html_response("QuantClaw login failed",
                                    "OpenAI returned an authorization error."),
                      "text/html");
    } else if (req.has_param("code")) {
      callback_state.done = true;
      callback_state.ok = true;
      callback_state.code = req.get_param_value("code");
      res.set_content(html_response("QuantClaw login complete",
                                    "Authorization succeeded. You can close "
                                    "this window."),
                      "text/html");
    } else {
      callback_state.done = true;
      callback_state.ok = false;
      callback_state.error = "Missing authorization code";
      res.set_content(html_response("QuantClaw login failed",
                                    "The callback did not include an "
                                    "authorization code."),
                      "text/html");
    }
    callback_state.cv.notify_all();
    std::thread([&server]() { server.stop(); }).detach();
  });

  std::thread server_thread([&server, bind_target]() {
    server.listen(bind_target.host.c_str(), bind_target.port);
  });

  out << "OpenAI Codex login\n";
  if (!open_browser(auth_url)) {
    out << "Open this URL in your browser:\n" << auth_url << "\n";
  }

  std::unique_lock<std::mutex> lock(callback_state.mu);
  if (!callback_state.cv.wait_for(
          lock, std::chrono::minutes(5),
          [&callback_state] { return callback_state.done; })) {
    out << "Browser callback timed out. Paste the full redirect URL or the "
           "authorization code:\n";
    std::string pasted;
    std::getline(in, pasted);
    callback_state.code = parse_manual_code(pasted);
    callback_state.ok = !callback_state.code.empty();
    callback_state.done = true;
    if (!callback_state.ok) {
      callback_state.error = "No authorization code provided";
    }
    server.stop();
  }
  lock.unlock();

  if (server_thread.joinable()) {
    server_thread.join();
  }

  if (!callback_state.ok) {
    throw std::runtime_error("OpenAI OAuth login failed: " +
                             callback_state.error);
  }

  return ExchangeCode(callback_state.code, code_verifier, redirect_uri);
}

OpenAICodexAuthRecord
OpenAICodexOAuthClient::Refresh(const OpenAICodexAuthRecord& record) {
  return RefreshWithToken(record.refresh_token);
}

std::string
OpenAICodexOAuthClient::ExtractAccountId(const std::string& access_token) {
  auto payload = parse_jwt_payload(access_token);
  if (!payload) {
    return "";
  }
  auto it = payload->find("https://api.openai.com/auth");
  if (it == payload->end() || !it->is_object()) {
    return "";
  }
  return it->value("chatgpt_account_id", "");
}

OpenAICodexAuthRecord
OpenAICodexOAuthClient::ExchangeCode(const std::string& code,
                                     const std::string& code_verifier,
                                     const std::string& redirect_uri) {
  const std::string body =
      "grant_type=authorization_code&client_id=" + url_encode(kClientId) +
      "&code=" + url_encode(code) +
      "&code_verifier=" + url_encode(code_verifier) +
      "&redirect_uri=" + url_encode(redirect_uri);
  auto token_json =
      nlohmann::json::parse(post_form(kTokenUrl, body, 30), nullptr, true);
  return token_record_from_json(token_json);
}

OpenAICodexAuthRecord
OpenAICodexOAuthClient::RefreshWithToken(const std::string& refresh_token) {
  const std::string body =
      "grant_type=refresh_token&client_id=" + url_encode(kClientId) +
      "&refresh_token=" + url_encode(refresh_token);
  auto token_json =
      nlohmann::json::parse(post_form(kTokenUrl, body, 30), nullptr, true);
  auto record = token_record_from_json(token_json);
  if (record.refresh_token.empty()) {
    record.refresh_token = refresh_token;
  }
  return record;
}

OpenAICodexCredentialResolver::OpenAICodexCredentialResolver(
    OpenAICodexAuthStore store, std::shared_ptr<OpenAICodexOAuthClient> client,
    std::shared_ptr<spdlog::logger> logger)
    : store_(std::move(store)),
      client_(std::move(client)),
      logger_(std::move(logger)) {}

std::string OpenAICodexCredentialResolver::ResolveAccessToken() {
  return ResolveAccessToken(now_epoch_seconds());
}

std::string OpenAICodexCredentialResolver::ResolveAccessToken(
    std::int64_t now_epoch_seconds) {
  auto record = store_.Load();
  if (!record.has_value()) {
    throw ProviderError(ProviderErrorKind::kAuthError, 401,
                        "OpenAI Codex is not logged in. Run `quantclaw models "
                        "auth login --provider openai-codex`.",
                        "openai-codex");
  }

  if (record->HasUsableAccessToken(now_epoch_seconds)) {
    return record->access_token;
  }

  if (!record->CanRefresh()) {
    throw ProviderError(
        ProviderErrorKind::kAuthError, 401,
        "OpenAI Codex credentials have expired and are not refreshable. Run "
        "`quantclaw models auth login --provider openai-codex` again.",
        "openai-codex");
  }

  try {
    auto refreshed = client_->Refresh(*record);
    if (refreshed.refresh_token.empty()) {
      refreshed.refresh_token = record->refresh_token;
    }
    store_.Save(refreshed);
    return refreshed.access_token;
  } catch (const std::exception& e) {
    if (logger_) {
      logger_->warn("OpenAI Codex token refresh failed: {}", e.what());
    }
    throw ProviderError(
        ProviderErrorKind::kAuthError, 401,
        "OpenAI Codex credentials could not be refreshed. Run `quantclaw "
        "models auth login --provider openai-codex` again.",
        "openai-codex");
  }
}

}  // namespace quantclaw::auth
