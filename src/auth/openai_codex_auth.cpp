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

#include <curl/curl.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "quantclaw/platform/process.hpp"
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
  for (unsigned char ch : value) {
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
  std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
  std::string cmd = "open \"" + url + "\"";
#else
  std::string cmd = "xdg-open \"" + url + "\"";
#endif
  return std::system(cmd.c_str()) == 0;
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
    return input;
  }
  auto pos = input.find('?');
  std::string query = pos == std::string::npos ? input : input.substr(pos + 1);
  return extract_query_param(query, "code");
}

}  // namespace

bool OpenAICodexAuthRecord::HasUsableAccessToken(std::int64_t now_epoch_seconds,
                                                 int leeway_seconds) const {
  return !access_token.empty() &&
         expires_at > (now_epoch_seconds + leeway_seconds);
}

bool OpenAICodexAuthRecord::CanRefresh() const {
  return !refresh_token.empty();
}

OpenAICodexAuthStore::OpenAICodexAuthStore(std::filesystem::path path)
    : path_(std::move(path)) {}

std::filesystem::path OpenAICodexAuthStore::DefaultPath() {
  return std::filesystem::path(platform::home_directory()) / ".quantclaw" /
         "auth" / "openai-codex.json";
}

bool OpenAICodexAuthStore::Exists() const {
  return std::filesystem::exists(path_);
}

std::optional<OpenAICodexAuthRecord> OpenAICodexAuthStore::Load() const {
  if (!Exists()) {
    return std::nullopt;
  }

  std::ifstream in(path_);
  if (!in) {
    throw std::runtime_error("Failed to open auth store: " + path_.string());
  }

  nlohmann::json j;
  in >> j;

  OpenAICodexAuthRecord record;
  record.provider = j.value("provider", "openai-codex");
  record.access_token = j.value("accessToken", "");
  record.refresh_token = j.value("refreshToken", "");
  record.token_type = j.value("tokenType", "Bearer");
  record.scope = j.value("scope", "");
  record.account_id = j.value("accountId", "");
  record.email = j.value("email", "");
  record.expires_at = j.value("expiresAt", static_cast<std::int64_t>(0));
  return record;
}

void OpenAICodexAuthStore::Save(const OpenAICodexAuthRecord& record) const {
  std::filesystem::create_directories(path_.parent_path());

  nlohmann::json j = {
      {"provider", record.provider},
      {"accessToken", record.access_token},
      {"refreshToken", record.refresh_token},
      {"tokenType", record.token_type},
      {"scope", record.scope},
      {"accountId", record.account_id},
      {"email", record.email},
      {"expiresAt", record.expires_at},
  };

  std::ofstream out(path_);
  if (!out) {
    throw std::runtime_error("Failed to write auth store: " + path_.string());
  }
  out << j.dump(2) << '\n';
  out.close();

#ifndef _WIN32
  std::filesystem::permissions(path_,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
#endif
}

bool OpenAICodexAuthStore::Clear() const {
  std::error_code ec;
  const bool removed = std::filesystem::remove(path_, ec);
  if (ec) {
    throw std::runtime_error("Failed to clear auth store: " + ec.message());
  }
  return removed;
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

OpenAICodexOAuthClient::OpenAICodexOAuthClient(
    std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

OpenAICodexAuthRecord
OpenAICodexOAuthClient::LoginInteractive(std::istream& in, std::ostream& out) {
  const std::string redirect_uri = kDefaultRedirectUri;
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

  std::thread server_thread([&server]() { server.listen("127.0.0.1", 1455); });

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
