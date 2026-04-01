// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <ostream>
#include <string>

#include <spdlog/spdlog.h>

#include "quantclaw/auth/provider_auth.hpp"

namespace quantclaw::auth {

using GitHubCopilotAuthRecord = ProviderAuthRecord;

class GitHubCopilotAuthStore : public ProviderAuthStore {
 public:
  explicit GitHubCopilotAuthStore(std::filesystem::path path = DefaultPath());

  static std::filesystem::path DefaultPath();
};

struct GitHubCopilotDeviceCodeResponse {
  std::string device_code;
  std::string user_code;
  std::string verification_uri;
  int expires_in = 0;
  int interval = 0;
};

class GitHubCopilotLoginClient {
 public:
  explicit GitHubCopilotLoginClient(std::shared_ptr<spdlog::logger> logger);
  virtual ~GitHubCopilotLoginClient() = default;

  virtual GitHubCopilotAuthRecord LoginInteractive(std::istream& in,
                                                   std::ostream& out);

 protected:
  virtual GitHubCopilotDeviceCodeResponse RequestDeviceCode();
  virtual std::string
  PollForAccessToken(const GitHubCopilotDeviceCodeResponse& device,
                     std::ostream& out);

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

struct GitHubCopilotRuntimeCredential {
  std::string api_token;
  std::string base_url;
  std::int64_t expires_at = 0;

  bool IsUsable(std::int64_t now_epoch_seconds, int leeway_seconds = 300) const;
};

class GitHubCopilotRuntimeResolverInterface {
 public:
  virtual ~GitHubCopilotRuntimeResolverInterface() = default;
  virtual GitHubCopilotRuntimeCredential ResolveRuntimeCredential() = 0;
};

class GitHubCopilotTokenCache {
 public:
  explicit GitHubCopilotTokenCache(std::filesystem::path path = DefaultPath());

  static std::filesystem::path DefaultPath();

  bool Exists() const;
  std::optional<GitHubCopilotRuntimeCredential> Load() const;
  void Save(const GitHubCopilotRuntimeCredential& credential) const;
  bool Clear() const;

 private:
  std::filesystem::path path_;
};

class GitHubCopilotTokenClient {
 public:
  explicit GitHubCopilotTokenClient(
      std::shared_ptr<spdlog::logger> logger,
      std::string copilot_token_url = DefaultCopilotTokenUrl());
  virtual ~GitHubCopilotTokenClient() = default;

  virtual GitHubCopilotRuntimeCredential
  ExchangeForApiToken(const std::string& github_token);

  static std::string DefaultCopilotTokenUrl();
  static std::string DeriveBaseUrlFromApiToken(const std::string& api_token);

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::string copilot_token_url_;
};

class GitHubCopilotRuntimeResolver
    : public GitHubCopilotRuntimeResolverInterface {
 public:
  GitHubCopilotRuntimeResolver(GitHubCopilotAuthStore store,
                               GitHubCopilotTokenCache cache,
                               std::shared_ptr<GitHubCopilotTokenClient> client,
                               std::shared_ptr<spdlog::logger> logger);

  GitHubCopilotRuntimeCredential ResolveRuntimeCredential() override;
  GitHubCopilotRuntimeCredential
  ResolveRuntimeCredential(std::int64_t now_epoch_seconds);

 private:
  GitHubCopilotAuthStore store_;
  GitHubCopilotTokenCache cache_;
  std::shared_ptr<GitHubCopilotTokenClient> client_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw::auth
