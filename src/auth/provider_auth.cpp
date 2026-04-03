// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/auth/provider_auth.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "quantclaw/platform/process.hpp"

namespace quantclaw::auth {

bool ProviderAuthRecord::HasUsableAccessToken(std::int64_t now_epoch_seconds,
                                              int leeway_seconds) const {
  if (access_token.empty()) {
    return false;
  }
  if (expires_at <= 0) {
    return true;
  }
  return expires_at > (now_epoch_seconds + leeway_seconds);
}

bool ProviderAuthRecord::CanRefresh() const {
  return !refresh_token.empty();
}

ProviderAuthStore::ProviderAuthStore(std::filesystem::path path)
    : path_(std::move(path)) {}

std::filesystem::path
ProviderAuthStore::DefaultPathFor(const std::string& provider_id) {
  return std::filesystem::path(platform::home_directory()) / ".quantclaw" /
         "auth" / (provider_id + ".json");
}

bool ProviderAuthStore::Exists() const {
  return std::filesystem::exists(path_);
}

std::optional<ProviderAuthRecord> ProviderAuthStore::Load() const {
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

  ProviderAuthRecord record;
  record.provider = j.value("provider", "");
  record.access_token = j.value("accessToken", "");
  record.refresh_token = j.value("refreshToken", "");
  record.token_type = j.value("tokenType", "Bearer");
  record.scope = j.value("scope", "");
  record.account_id = j.value("accountId", "");
  record.email = j.value("email", "");
  record.expires_at = j.value("expiresAt", static_cast<std::int64_t>(0));
  return record;
}

void ProviderAuthStore::Save(const ProviderAuthRecord& record) const {
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

  const auto temp_path =
      path_.parent_path() / (path_.filename().string() + ".tmp");
#ifndef _WIN32
  {
    std::ofstream create(temp_path, std::ios::trunc);
    if (!create) {
      throw std::runtime_error("Failed to write auth store: " +
                               temp_path.string());
    }
  }
  std::filesystem::permissions(temp_path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
#endif
  std::ofstream out(temp_path, std::ios::trunc);
  if (!out) {
    throw std::runtime_error("Failed to write auth store: " +
                             temp_path.string());
  }
  out << j.dump(2) << '\n';
  out.close();
  std::error_code remove_ec;
  std::filesystem::remove(path_, remove_ec);
  std::filesystem::rename(temp_path, path_);
}

bool ProviderAuthStore::Clear() const {
  std::error_code ec;
  const bool removed = std::filesystem::remove(path_, ec);
  if (ec) {
    throw std::runtime_error("Failed to clear auth store: " + ec.message());
  }
  return removed;
}

}  // namespace quantclaw::auth
