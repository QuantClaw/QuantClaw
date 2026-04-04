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
      continue;
    }
    ++i;
  }
  return true;
}

template <typename Record>
bool is_logged_in(const Record& record) {
  return !record.access_token.empty() || !record.refresh_token.empty();
}

std::int64_t now_epoch_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int handle_openai_codex_status(ModelAuthCommandContext& ctx,
                               std::ostream& out) {
  auto record = ctx.openai_codex_store.Load();
  if (!record.has_value() || !is_logged_in(*record)) {
    out << "Not logged in to OpenAI Codex\n";
    return 0;
  }

  const auto now = now_epoch_seconds();
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

int handle_github_copilot_status(ModelAuthCommandContext& ctx,
                                 std::ostream& out) {
  auto record = ctx.github_copilot_store.Load();
  if (!record.has_value() || !is_logged_in(*record)) {
    out << "Not logged in to GitHub Copilot\n";
    return 0;
  }

  const auto now = now_epoch_seconds();
  out << "Provider: github-copilot\n";
  out << "Status: "
      << (record->HasUsableAccessToken(now) ? "logged in" : "token expired")
      << "\n";
  out << "Refreshable: " << (record->CanRefresh() ? "yes" : "no") << "\n";
  if (!record->account_id.empty()) {
    out << "Account ID: " << record->account_id << "\n";
  }
  out << "Expires: " << format_expiry(record->expires_at) << "\n";
  return 0;
}

int handle_openai_codex_login(ModelAuthCommandContext& ctx, std::istream& in,
                              std::ostream& out) {
  auto record = ctx.openai_codex_client->LoginInteractive(in, out);
  ctx.openai_codex_store.Save(record);
  out << "Logged in to OpenAI Codex";
  if (!record.account_id.empty()) {
    out << " (" << record.account_id << ")";
  }
  out << "\n";
  return 0;
}

int handle_github_copilot_login(ModelAuthCommandContext& ctx, std::istream& in,
                                std::ostream& out, std::ostream& err) {
  if (!ctx.stdin_is_tty) {
    err << "GitHub Copilot login requires an interactive terminal\n";
    return 1;
  }

  auto record = ctx.github_copilot_client->LoginInteractive(in, out);
  ctx.github_copilot_store.Save(record);
  out << "Logged in to GitHub Copilot";
  if (!record.account_id.empty()) {
    out << " (" << record.account_id << ")";
  }
  out << "\n";
  return 0;
}

int handle_openai_codex_logout(ModelAuthCommandContext& ctx,
                               std::ostream& out) {
  ctx.openai_codex_store.Clear();
  out << "Logged out from OpenAI Codex\n";
  return 0;
}

int handle_github_copilot_logout(ModelAuthCommandContext& ctx,
                                 std::ostream& out) {
  ctx.github_copilot_store.Clear();
  out << "Logged out from GitHub Copilot\n";
  return 0;
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
  if (!ctx.openai_codex_client) {
    ctx.openai_codex_client =
        std::make_shared<auth::OpenAICodexOAuthClient>(ctx.logger);
  }
  if (!ctx.github_copilot_client) {
    ctx.github_copilot_client =
        std::make_shared<auth::GitHubCopilotLoginClient>(ctx.logger);
  }

  auto& in = *ctx.in;
  auto& out = *ctx.out;
  auto& err = *ctx.err;

  const std::string subcommand = args.empty() ? "status" : args.front();
  std::string provider;
  if (!parse_provider_flag(args, &provider)) {
    err << "Usage: quantclaw models auth <login|status|logout> --provider "
           "<openai-codex|github-copilot>\n";
    return 1;
  }

  if (provider != "openai-codex" && provider != "github-copilot") {
    err << "Supported providers: openai-codex, github-copilot\n";
    return 1;
  }

  try {
    if (subcommand == "login-github-copilot") {
      provider = "github-copilot";
    }

    if (subcommand == "status") {
      if (provider == "openai-codex") {
        return handle_openai_codex_status(ctx, out);
      }
      return handle_github_copilot_status(ctx, out);
    }

    if (subcommand == "login" || subcommand == "login-github-copilot") {
      if (provider == "openai-codex") {
        return handle_openai_codex_login(ctx, in, out);
      }
      return handle_github_copilot_login(ctx, in, out, err);
    }

    if (subcommand == "logout") {
      if (provider == "openai-codex") {
        return handle_openai_codex_logout(ctx, out);
      }
      return handle_github_copilot_logout(ctx, out);
    }

    err << "Usage: quantclaw models auth "
           "<login|login-github-copilot|status|logout> --provider "
           "<openai-codex|github-copilot>\n";
    return 1;
  } catch (const std::exception& e) {
    err << "Error: " << e.what() << "\n";
    return 1;
  }
}

}  // namespace quantclaw::cli
