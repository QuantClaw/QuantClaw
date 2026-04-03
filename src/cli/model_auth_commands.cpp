// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/cli/model_auth_commands.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace quantclaw::cli {
namespace {

std::string format_expiry(std::int64_t expires_at) {
  if (expires_at <= 0) {
    return "unknown";
  }
  std::time_t t = static_cast<std::time_t>(expires_at);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S UTC");
  return out.str();
}

bool parse_provider_flag(const std::vector<std::string>& args,
                         std::string* provider) {
  *provider = "openai-codex";
  size_t i = 0;
  while (i < args.size()) {
    if (args[i] == "--provider") {
      if (i + 1 >= args.size()) {
        return false;
      }
      *provider = args[i + 1];
      i += 2;
    } else {
      ++i;
    }
  }
  return true;
}

bool is_logged_in(const auth::OpenAICodexAuthRecord& record) {
  return !record.access_token.empty() || !record.refresh_token.empty();
}

}  // namespace

int HandleModelsAuthCommand(const std::vector<std::string>& args,
                            ModelAuthCommandContext ctx) {
  if (!ctx.in) {
    ctx.in = &std::cin;
  }
  if (!ctx.out) {
    ctx.out = &std::cout;
  }
  if (!ctx.err) {
    ctx.err = &std::cerr;
  }
  if (!ctx.oauth_client) {
    ctx.oauth_client =
        std::make_shared<auth::OpenAICodexOAuthClient>(ctx.logger);
  }

  auto& in = *ctx.in;
  auto& out = *ctx.out;
  auto& err = *ctx.err;

  const std::string subcommand = args.empty() ? "status" : args.front();
  std::string provider;
  if (!parse_provider_flag(args, &provider)) {
    err << "Usage: quantclaw models auth <login|status|logout> --provider "
           "openai-codex\n";
    return 1;
  }

  if (provider != "openai-codex") {
    err << "Only --provider openai-codex is currently supported\n";
    return 1;
  }

  try {
    if (subcommand == "status") {
      auto record = ctx.store.Load();
      if (!record.has_value() || !is_logged_in(*record)) {
        out << "Not logged in to OpenAI Codex\n";
        return 0;
      }

      const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      out << "Provider: openai-codex\n";
      out << "Status: "
          << (record->HasUsableAccessToken(now) ? "logged in" : "token expired")
          << "\n";
      out << "Refreshable: " << (record->CanRefresh() ? "yes" : "no") << "\n";
      if (!record->account_id.empty()) {
        out << "Account ID: " << record->account_id << "\n";
      }
      if (!record->email.empty()) {
        out << "Email: " << record->email << "\n";
      }
      out << "Expires: " << format_expiry(record->expires_at) << "\n";
      return 0;
    }

    if (subcommand == "login") {
      auto record = ctx.oauth_client->LoginInteractive(in, out);
      ctx.store.Save(record);
      out << "Logged in to OpenAI Codex";
      if (!record.account_id.empty()) {
        out << " (" << record.account_id << ")";
      }
      out << "\n";
      return 0;
    }

    if (subcommand == "logout") {
      ctx.store.Clear();
      out << "Logged out from OpenAI Codex\n";
      return 0;
    }

    err << "Usage: quantclaw models auth <login|status|logout> --provider "
           "openai-codex\n";
    return 1;
  } catch (const std::exception& e) {
    err << "Error: " << e.what() << "\n";
    return 1;
  }
}

}  // namespace quantclaw::cli
