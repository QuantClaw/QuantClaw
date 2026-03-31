// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.default_context_engine;

import std;
import quantclaw.config;
import quantclaw.core.context_engine;
import quantclaw.providers.llm_provider;
import quantclaw.core.context_pruner;
import quantclaw.core.multi_stage_compaction;

export namespace quantclaw {

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