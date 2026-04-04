// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/auth/provider_auth.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include "quantclaw/platform/process.hpp"

namespace quantclaw::auth {
namespace {

#ifdef _WIN32
bool ReplaceFileAtomicallyWindows(const std::filesystem::path& from,
                                  const std::filesystem::path& to,
                                  std::string* error) {
  if (MoveFileExW(from.c_str(), to.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
  if (error != nullptr) {
    *error = std::error_code(static_cast<int>(GetLastError()),
                             std::system_category())
                 .message();
  }
  return false;
}

detail::ProviderAuthReplaceFileFn g_provider_auth_replace_file_fn =
    &ReplaceFileAtomicallyWindows;
#else
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
      throw std::runtime_error("Failed to write auth store: " + path.string());
    }
    if (written == 0) {
      throw std::runtime_error("Failed to write auth store: " + path.string());
    }
    total_written += static_cast<size_t>(written);
  }
}
#endif

}  // namespace

#ifdef _WIN32
void detail::SetProviderAuthReplaceFileFnForTest(ProviderAuthReplaceFileFn fn) {
  g_provider_auth_replace_file_fn =
      fn != nullptr ? fn : &ReplaceFileAtomicallyWindows;
}

void detail::ResetProviderAuthReplaceFileFnForTest() {
  g_provider_auth_replace_file_fn = &ReplaceFileAtomicallyWindows;
}
#endif

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
  const std::string content = j.dump(2) + "\n";

#ifndef _WIN32
  int fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR);
  if (fd < 0) {
    throw std::runtime_error("Failed to write auth store: " +
                             temp_path.string());
  }

  try {
    WriteAllOrThrow(fd, content, temp_path);
    if (::close(fd) != 0) {
      fd = -1;
      throw std::runtime_error("Failed to write auth store: " +
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
  {
    std::ofstream out(temp_path, std::ios::trunc);
    if (!out) {
      throw std::runtime_error("Failed to write auth store: " +
                               temp_path.string());
    }
    out << content;
    out.close();
  }
  std::string replace_error;
  if (!g_provider_auth_replace_file_fn(temp_path, path_, &replace_error)) {
    std::error_code cleanup_ec;
    std::filesystem::remove(temp_path, cleanup_ec);
    throw std::runtime_error("Failed to replace auth store: " + replace_error);
  }
#endif
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
