// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.providers.provider_error;

import std;

export namespace quantclaw {

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
                const std::string& message,
                const std::string& provider_id = "",
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

inline ProviderError::ProviderError(ProviderErrorKind kind, int http_status,
                                    const std::string& message,
                                    const std::string& provider_id,
                                    const std::string& profile_id)
    : std::runtime_error(message),
      kind_(kind),
      http_status_(http_status),
      provider_id_(provider_id),
      profile_id_(profile_id) {}

inline std::string ProviderErrorKindToString(ProviderErrorKind kind) {
  switch (kind) {
    case ProviderErrorKind::kRateLimit:
      return "rate_limit";
    case ProviderErrorKind::kAuthError:
      return "auth_error";
    case ProviderErrorKind::kBillingError:
      return "billing_error";
    case ProviderErrorKind::kTransient:
      return "transient";
    case ProviderErrorKind::kModelNotFound:
      return "model_not_found";
    case ProviderErrorKind::kTimeout:
      return "timeout";
    case ProviderErrorKind::kContextOverflow:
      return "context_overflow";
    case ProviderErrorKind::kUnknown:
    default:
      return "unknown";
  }
}

inline ProviderErrorKind ClassifyHttpError(int http_status,
                                           const std::string& response_body) {
  if (http_status == 429)
    return ProviderErrorKind::kRateLimit;
  if (http_status == 401 || http_status == 403)
    return ProviderErrorKind::kAuthError;
  if (http_status == 402)
    return ProviderErrorKind::kBillingError;
  if (http_status == 404)
    return ProviderErrorKind::kModelNotFound;
  if (http_status == 408)
    return ProviderErrorKind::kTimeout;
  if (http_status == 500 || http_status == 502 || http_status == 503 ||
      http_status == 504)
    return ProviderErrorKind::kTransient;

  auto body_lower = response_body;
  for (char& c : body_lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  if (body_lower.find("insufficient_credits") != std::string::npos ||
      body_lower.find("insufficient_quota") != std::string::npos ||
      body_lower.find("billing") != std::string::npos) {
    return ProviderErrorKind::kBillingError;
  }
  if (body_lower.find("context_length") != std::string::npos ||
      body_lower.find("context window") != std::string::npos ||
      body_lower.find("context_length_exceeded") != std::string::npos) {
    return ProviderErrorKind::kContextOverflow;
  }
  if (body_lower.find("rate limit") != std::string::npos ||
      body_lower.find("too many requests") != std::string::npos) {
    return ProviderErrorKind::kRateLimit;
  }

  return ProviderErrorKind::kUnknown;
}

}  // namespace quantclaw
