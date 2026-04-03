// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/github_copilot_provider.hpp"

#include <chrono>

namespace quantclaw {
namespace {

constexpr char kCopilotUserAgent[] = "GithubCopilot/1.155.0";
constexpr char kCopilotEditorVersion[] = "vscode/1.85.1";
constexpr char kCopilotEditorPluginVersion[] = "copilot/1.155.0";

}  // namespace

GitHubCopilotProvider::GitHubCopilotProvider(
    int timeout, std::shared_ptr<spdlog::logger> logger,
    std::shared_ptr<auth::GitHubCopilotRuntimeResolverInterface> resolver)
    : OpenAIProvider("", "https://api.individual.githubcopilot.com", timeout,
                     std::move(logger)),
      resolver_(std::move(resolver)) {}

std::string GitHubCopilotProvider::GetProviderName() const {
  return "github-copilot";
}

std::vector<std::string> GitHubCopilotProvider::GetSupportedModels() const {
  return {"gpt-4o", "gpt-4.1", "claude-3.7-sonnet", "claude-sonnet-4"};
}

const auth::GitHubCopilotRuntimeCredential&
GitHubCopilotProvider::ResolveRuntimeCredential() const {
  std::lock_guard<std::mutex> lock(runtime_mu_);
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  if (!cached_runtime_.has_value() || !cached_runtime_->IsUsable(now)) {
    cached_runtime_ = resolver_->ResolveRuntimeCredential();
  }
  return *cached_runtime_;
}

std::string GitHubCopilotProvider::ResolveApiKey() const {
  return ResolveRuntimeCredential().api_token;
}

std::string GitHubCopilotProvider::ResolveBaseUrl() const {
  const auto& runtime = ResolveRuntimeCredential();
  return runtime.base_url.empty() ? OpenAIProvider::ResolveBaseUrl()
                                  : runtime.base_url;
}

std::string GitHubCopilotProvider::ProviderId() const {
  return "github-copilot";
}

CurlSlist GitHubCopilotProvider::CreateHeaders() const {
  auto headers = OpenAIProvider::CreateHeaders();
  headers.append(("User-Agent: " + std::string(kCopilotUserAgent)).c_str());
  headers.append(
      ("Editor-Version: " + std::string(kCopilotEditorVersion)).c_str());
  headers.append(
      ("Editor-Plugin-Version: " + std::string(kCopilotEditorPluginVersion))
          .c_str());
  return headers;
}

}  // namespace quantclaw
