// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/provider_error.hpp"

namespace quantclaw {

std::string ProviderErrorKindToString(ProviderErrorKind kind) {
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
      return "unknown";
  }
  return "unknown";
}

ProviderError::ProviderError(ProviderErrorKind kind, int http_status,
                             const std::string& message,
                             const std::string& provider_id,
                             const std::string& profile_id)
    : std::runtime_error(message),
      kind_(kind),
      http_status_(http_status),
      provider_id_(provider_id),
      profile_id_(profile_id) {}

ProviderErrorKind ClassifyHttpError(int http_status,
                                    const std::string& response_body) {
  switch (http_status) {
    case 429:
      return ProviderErrorKind::kRateLimit;

    case 401:
    case 403:
      return ProviderErrorKind::kAuthError;

    case 402:
      return ProviderErrorKind::kBillingError;

    case 404:
      return ProviderErrorKind::kModelNotFound;

    case 500:
    case 502:
    case 503:
    case 504:
      return ProviderErrorKind::kTransient;

    default:
      break;
  }

  // Check response body for specific error categories
  if (!response_body.empty()) {
    if (response_body.find("insufficient_credits") != std::string::npos ||
        response_body.find("insufficient_quota") != std::string::npos ||
        response_body.find("billing") != std::string::npos) {
      return ProviderErrorKind::kBillingError;
    }
    // Context window / token limit exceeded
    if (response_body.find("context_length") != std::string::npos ||
        response_body.find("context_window") != std::string::npos ||
        response_body.find("token limit") != std::string::npos ||
        response_body.find("maximum context length") != std::string::npos ||
        response_body.find("max_tokens") != std::string::npos) {
      return ProviderErrorKind::kContextOverflow;
    }
  }

  if (http_status >= 400 && http_status < 500) {
    return ProviderErrorKind::kUnknown;
  }
  if (http_status >= 500) {
    return ProviderErrorKind::kTransient;
  }

  return ProviderErrorKind::kUnknown;
}

}  // namespace quantclaw
