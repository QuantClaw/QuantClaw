// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace quantclaw {

struct EmbeddingRequest {
  std::vector<std::string> texts;
  std::string model = "text-embedding-3-small";
};

struct EmbeddingResponse {
  std::vector<std::vector<float>> embeddings;
  int total_tokens = 0;
};

// Abstract embedding provider interface.
// Implementations can use OpenAI, local models, or any embedding API.
class EmbeddingProvider {
 public:
  virtual ~EmbeddingProvider() = default;

  // Embed one or more texts.
  virtual EmbeddingResponse Embed(const EmbeddingRequest& request) = 0;

  // Embedding dimensionality.
  virtual int Dimensions() const = 0;

  // Provider name (for logging).
  virtual std::string Name() const = 0;
};

}  // namespace quantclaw
