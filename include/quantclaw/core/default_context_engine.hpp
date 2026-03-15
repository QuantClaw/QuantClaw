// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/core/context_engine.hpp"
#include "quantclaw/core/multi_stage_compaction.hpp"

namespace quantclaw {

// Default context engine: wraps existing ContextPruner + auto-compaction +
// context window guard logic that was previously inline in AgentLoop.
// Supports multi-stage compaction when a summary function is provided.
class DefaultContextEngine : public ContextEngine {
 public:
  DefaultContextEngine(const AgentConfig& config,
                       std::shared_ptr<spdlog::logger> logger);

  std::string Name() const override {
    return "default";
  }

  AssembleResult Assemble(const std::vector<Message>& history,
                          const std::string& system_prompt,
                          const std::string& user_message, int context_window,
                          int max_tokens) override;

  std::vector<Message> CompactOverflow(const std::vector<Message>& messages,
                                       const std::string& system_prompt,
                                       int keep_recent) override;

  // Set a summary function for multi-stage compaction.
  // When set, CompactOverflow will use chunk-and-merge instead of truncation.
  void SetSummaryFn(SummaryFn fn) {
    summary_fn_ = std::move(fn);
  }

  void SetConfig(const AgentConfig& config) {
    config_ = config;
  }

 private:
  AgentConfig config_;
  std::shared_ptr<spdlog::logger> logger_;
  MultiStageCompaction compactor_;
  SummaryFn summary_fn_;
};

}  // namespace quantclaw
