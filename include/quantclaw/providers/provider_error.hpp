// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdexcept>
#include <string>

namespace quantclaw {

// Classification of provider API errors, used for failover decisions.
enum class ProviderErrorKind {
  kRateLimit,        // 429 Too Many Requests
  kAuthError,        // 401/403 Unauthorized/Forbidden
  kBillingError,     // 402 / "insufficient_credits" in body
  kTransient,        // 500/502/503/504 Server Error
  kModelNotFound,    // 404 Not Found
  kTimeout,          // CURL timeout or network error
  kContextOverflow,  // Context window exceeded (400 + "context_length" in body)
  kUnknown,          // Unclassified error
};

std::string ProviderErrorKindToString(ProviderErrorKind kind);

// Exception thrown by LLM providers when an API call fails.
// Carries the error classification so failover logic can decide
// whether to retry, cool down, or fall back to another model.
class ProviderError : public std::runtime_error {
 public:
  ProviderError(ProviderErrorKind kind, int http_status,
                const std::string& message, const std::string& provider_id = "",
                const std::string& profile_id = "");

  ProviderErrorKind Kind() const {
    return kind_;
  }
  int HttpStatus() const {
    return http_status_;
  }
  const std::string& ProviderId() const {
    return provider_id_;
  }
  const std::string& ProfileId() const {
    return profile_id_;
  }

  // Server-provided Retry-After value in seconds (0 = not provided).
  int RetryAfterSeconds() const {
    return retry_after_seconds_;
  }
  void SetRetryAfterSeconds(int seconds) {
    retry_after_seconds_ = seconds;
  }

 private:
  ProviderErrorKind kind_;
  int http_status_;
  std::string provider_id_;
  std::string profile_id_;
  int retry_after_seconds_ = 0;
};

// Classify an HTTP status code (and optional response body) into
// a ProviderErrorKind.
ProviderErrorKind ClassifyHttpError(int http_status,
                                    const std::string& response_body = "");

}  // namespace quantclaw
