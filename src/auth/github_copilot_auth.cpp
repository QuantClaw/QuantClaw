// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/auth/github_copilot_auth.hpp"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "quantclaw/providers/curl_raii.hpp"
#include "quantclaw/providers/provider_error.hpp"

namespace quantclaw::auth {
namespace {

constexpr char kClientId[] = "Iv1.b507a08c87ecfe98";
constexpr char kDeviceCodeUrl[] = "https://github.com/login/device/code";
constexpr char kAccessTokenUrl[] =
    "https://github.com/login/oauth/access_token";
constexpr char kCopilotTokenUrl[] =
    "https://api.github.com/copilot_internal/v2/token";
constexpr char kDefaultCopilotBaseUrl[] =
    "https://api.individual.githubcopilot.com";

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

std::string post_form(const std::string& url, const std::string& body) {
  std::string response_body;
  CurlHandle curl;
  CurlSlist headers;
  headers.append("Accept: application/json");
  headers.append("Content-Type: application/x-www-form-urlencoded");
  headers.append("User-Agent: QuantClaw/0.3.0");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  const auto code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    throw std::runtime_error("GitHub auth request failed: " +
                             std::string(curl_easy_strerror(code)));
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400) {
    throw std::runtime_error("GitHub auth endpoint returned HTTP " +
                             std::to_string(http_code) + ": " + response_body);
  }
  return response_body;
}

std::string get_json(const std::string& url, const std::string& bearer_token) {
  std::string response_body;
  CurlHandle curl;
  CurlSlist headers;
  headers.append("Accept: application/json");
  headers.append("User-Agent: QuantClaw/0.3.0");
  headers.append(("Authorization: Bearer " + bearer_token).c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  const auto code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    throw std::runtime_error("GitHub Copilot token exchange failed: " +
                             std::string(curl_easy_strerror(code)));
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400) {
    throw std::runtime_error("GitHub Copilot token endpoint returned HTTP " +
                             std::to_string(http_code) + ": " + response_body);
  }

  return response_body;
}

std::string trim(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                            value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  size_t start = 0;
  while (start < value.size() &&
         (value[start] == ' ' || value[start] == '\t' || value[start] == '\n' ||
          value[start] == '\r')) {
    ++start;
  }
  return value.substr(start);
}

#ifndef _WIN32
void WriteAllOrThrow(int fd, std::string_view content,
                     const std::filesystem::path& path) {
  size_t total_written = 0;
  while (total_written < content.size()) {
    const auto* data = content.data() + total_written;
    const auto remaining = content.size() - total_written;
    const ssize_t written = ::write(fd, data, remaining);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("Failed to write Copilot token cache: " +
                               path.string());
    }
    if (written == 0) {
      throw std::runtime_error("Failed to write Copilot token cache: " +
                               path.string());
    }
    total_written += static_cast<size_t>(written);
  }
}
#endif

}  // namespace

GitHubCopilotAuthStore::GitHubCopilotAuthStore(std::filesystem::path path)
    : ProviderAuthStore(std::move(path)) {}

std::filesystem::path GitHubCopilotAuthStore::DefaultPath() {
  return DefaultPathFor("github-copilot");
}

GitHubCopilotLoginClient::GitHubCopilotLoginClient(
    std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

GitHubCopilotAuthRecord
GitHubCopilotLoginClient::LoginInteractive(std::istream& /*in*/,
                                           std::ostream& out) {
  const auto device = RequestDeviceCode();
  out << "GitHub Copilot login\n";
  out << "Visit: " << device.verification_uri << "\n";
  out << "Code: " << device.user_code << "\n";
  out << "Waiting for GitHub authorization...\n";

  GitHubCopilotAuthRecord record;
  record.provider = "github-copilot";
  record.access_token = PollForAccessToken(device, out);
  record.token_type = "Bearer";
  record.expires_at = 0;
  return record;
}

GitHubCopilotDeviceCodeResponse GitHubCopilotLoginClient::RequestDeviceCode() {
  const auto body = "client_id=" + std::string(kClientId) + "&scope=read:user";
  const auto json = nlohmann::json::parse(post_form(kDeviceCodeUrl, body));

  GitHubCopilotDeviceCodeResponse response;
  response.device_code = json.value("device_code", "");
  response.user_code = json.value("user_code", "");
  response.verification_uri = json.value("verification_uri", "");
  response.expires_in = json.value("expires_in", 0);
  response.interval = json.value("interval", 5);
  if (response.device_code.empty() || response.user_code.empty() ||
      response.verification_uri.empty()) {
    throw std::runtime_error("GitHub device code response missing fields");
  }
  return response;
}

std::string GitHubCopilotLoginClient::PollForAccessToken(
    const GitHubCopilotDeviceCodeResponse& device, std::ostream& /*out*/) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(device.expires_in);
  auto interval = std::max(device.interval, 1);

  while (std::chrono::steady_clock::now() < deadline) {
    const std::string body =
        "client_id=" + std::string(kClientId) +
        "&device_code=" + device.device_code +
        "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
    const auto json = nlohmann::json::parse(post_form(kAccessTokenUrl, body));

    if (json.contains("access_token") && json["access_token"].is_string()) {
      return json["access_token"].get<std::string>();
    }

    const auto error = json.value("error", std::string{});
    if (error == "authorization_pending") {
      std::this_thread::sleep_for(std::chrono::seconds(interval));
      continue;
    }
    if (error == "slow_down") {
      interval += 2;
      std::this_thread::sleep_for(std::chrono::seconds(interval));
      continue;
    }
    if (error == "expired_token") {
      throw std::runtime_error("GitHub device code expired; run login again");
    }
    if (error == "access_denied") {
      throw std::runtime_error("GitHub login cancelled");
    }
    throw std::runtime_error("GitHub device flow error: " + error);
  }

  throw std::runtime_error("GitHub device code expired; run login again");
}

bool GitHubCopilotRuntimeCredential::IsUsable(std::int64_t now_epoch_seconds,
                                              int leeway_seconds) const {
  return !api_token.empty() &&
         expires_at > (now_epoch_seconds + leeway_seconds);
}

GitHubCopilotTokenCache::GitHubCopilotTokenCache(std::filesystem::path path)
    : path_(std::move(path)) {}

std::filesystem::path GitHubCopilotTokenCache::DefaultPath() {
  return GitHubCopilotAuthStore::DefaultPath().parent_path() /
         "github-copilot.token-cache.json";
}

bool GitHubCopilotTokenCache::Exists() const {
  return std::filesystem::exists(path_);
}

std::optional<GitHubCopilotRuntimeCredential>
GitHubCopilotTokenCache::Load() const {
  if (!Exists()) {
    return std::nullopt;
  }
  std::ifstream in(path_);
  if (!in) {
    return std::nullopt;
  }
  nlohmann::json j;
  try {
    in >> j;
  } catch (const std::exception&) {
    return std::nullopt;
  }
  GitHubCopilotRuntimeCredential record;
  record.api_token = j.value("apiToken", "");
  record.base_url = j.value("baseUrl", std::string(kDefaultCopilotBaseUrl));
  record.expires_at = j.value("expiresAt", static_cast<std::int64_t>(0));
  return record;
}

void GitHubCopilotTokenCache::Save(
    const GitHubCopilotRuntimeCredential& credential) const {
  std::filesystem::create_directories(path_.parent_path());
  nlohmann::json j = {{"apiToken", credential.api_token},
                      {"baseUrl", credential.base_url},
                      {"expiresAt", credential.expires_at}};
  const auto temp_path =
      path_.parent_path() / (path_.filename().string() + ".tmp");
  const std::string content = j.dump(2) + "\n";

#ifndef _WIN32
  int fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR);
  if (fd < 0) {
    throw std::runtime_error("Failed to write Copilot token cache: " +
                             temp_path.string());
  }

  try {
    WriteAllOrThrow(fd, content, temp_path);
    if (::close(fd) != 0) {
      fd = -1;
      throw std::runtime_error("Failed to write Copilot token cache: " +
                               temp_path.string());
    }
    fd = -1;
    std::filesystem::rename(temp_path, path_);
  } catch (...) {
    if (fd >= 0) {
      ::close(fd);
    }
    std::error_code cleanup_ec;
    std::filesystem::remove(temp_path, cleanup_ec);
    throw;
  }
#else
  std::ofstream out(temp_path, std::ios::trunc);
  if (!out) {
    throw std::runtime_error("Failed to write Copilot token cache: " +
                             temp_path.string());
  }
  out << content;
  out.close();
  if (!MoveFileExW(temp_path.c_str(), path_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code cleanup_ec;
    std::filesystem::remove(temp_path, cleanup_ec);
    throw std::runtime_error("Failed to replace Copilot token cache: " +
                             std::error_code(static_cast<int>(GetLastError()),
                                             std::system_category())
                                 .message());
  }
#endif
}

bool GitHubCopilotTokenCache::Clear() const {
  std::error_code ec;
  const bool removed = std::filesystem::remove(path_, ec);
  if (ec) {
    throw std::runtime_error("Failed to clear Copilot token cache: " +
                             ec.message());
  }
  return removed;
}

GitHubCopilotTokenClient::GitHubCopilotTokenClient(
    std::shared_ptr<spdlog::logger> logger, std::string copilot_token_url)
    : logger_(std::move(logger)),
      copilot_token_url_(std::move(copilot_token_url)) {}

std::string GitHubCopilotTokenClient::DefaultCopilotTokenUrl() {
  return kCopilotTokenUrl;
}

GitHubCopilotRuntimeCredential
GitHubCopilotTokenClient::ExchangeForApiToken(const std::string& github_token) {
  const auto json =
      nlohmann::json::parse(get_json(copilot_token_url_, github_token));
  GitHubCopilotRuntimeCredential credential;
  credential.api_token = json.value("token", "");
  if (json.contains("expires_at")) {
    const auto& expires_at = json["expires_at"];
    if (expires_at.is_number_integer()) {
      credential.expires_at = expires_at.get<std::int64_t>();
    } else if (expires_at.is_string()) {
      credential.expires_at = std::stoll(expires_at.get<std::string>());
    }
  }
  if (credential.expires_at > 0 && credential.expires_at < 100000000000LL) {
    credential.expires_at *= 1000;
  }
  if (credential.expires_at > 0) {
    credential.expires_at /= 1000;
  }
  credential.base_url = DeriveBaseUrlFromApiToken(credential.api_token);
  if (credential.base_url.empty()) {
    credential.base_url = kDefaultCopilotBaseUrl;
  }
  if (credential.api_token.empty()) {
    throw std::runtime_error("Copilot token response missing token");
  }
  return credential;
}

std::string GitHubCopilotTokenClient::DeriveBaseUrlFromApiToken(
    const std::string& api_token) {
  const auto trimmed = trim(api_token);
  if (trimmed.empty()) {
    return "";
  }
  const auto pos = trimmed.find("proxy-ep=");
  if (pos == std::string::npos) {
    return "";
  }
  auto host = trimmed.substr(pos + 9);
  const auto end = host.find(';');
  if (end != std::string::npos) {
    host = host.substr(0, end);
  }
  host = trim(host);
  if (host.rfind("https://", 0) == 0) {
    host.erase(0, 8);
  } else if (host.rfind("http://", 0) == 0) {
    host.erase(0, 7);
  }
  if (host.rfind("proxy.", 0) == 0) {
    host.replace(0, 6, "api.");
  }
  return host.empty() ? "" : "https://" + host;
}

GitHubCopilotRuntimeResolver::GitHubCopilotRuntimeResolver(
    GitHubCopilotAuthStore store, GitHubCopilotTokenCache cache,
    std::shared_ptr<GitHubCopilotTokenClient> client,
    std::shared_ptr<spdlog::logger> logger,
    GitHubTokenResolver env_token_resolver)
    : store_(std::move(store)),
      cache_(std::move(cache)),
      client_(std::move(client)),
      logger_(std::move(logger)),
      env_token_resolver_(std::move(env_token_resolver)) {}

GitHubCopilotRuntimeCredential
GitHubCopilotRuntimeResolver::ResolveRuntimeCredential() {
  return ResolveRuntimeCredential(now_epoch_seconds());
}

GitHubCopilotRuntimeCredential
GitHubCopilotRuntimeResolver::ResolveRuntimeCredential(
    std::int64_t now_epoch_seconds) {
  auto github_token =
      env_token_resolver_ ? env_token_resolver_() : std::string();
  if (!github_token.empty()) {
    auto runtime = client_->ExchangeForApiToken(github_token);
    cache_.Save(runtime);
    return runtime;
  }

  if (auto cached = cache_.Load();
      cached.has_value() && cached->IsUsable(now_epoch_seconds)) {
    return *cached;
  }

  auto record = store_.Load();
  if (!record.has_value() || record->access_token.empty()) {
    throw ProviderError(
        ProviderErrorKind::kAuthError, 401,
        "GitHub Copilot is not logged in. Run `quantclaw models "
        "auth login --provider github-copilot`.",
        "github-copilot");
  }

  auto runtime = client_->ExchangeForApiToken(record->access_token);
  cache_.Save(runtime);
  return runtime;
}

}  // namespace quantclaw::auth
