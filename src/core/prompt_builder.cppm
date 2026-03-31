// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.core.prompt_builder;

import std;
import quantclaw.config;
import quantclaw.core.memory_manager;
import quantclaw.core.skill_loader;
import quantclaw.tools.tool_registry;

export namespace quantclaw {

class PromptBuilder {
 public:
  PromptBuilder(std::shared_ptr<MemoryManager> memory_manager,
                std::shared_ptr<SkillLoader> skill_loader,
                std::shared_ptr<ToolRegistry> tool_registry,
                const QuantClawConfig* config = nullptr);

  std::string BuildFull(const std::string& agent_id = "default") const;
  std::string BuildMinimal(const std::string& agent_id = "default") const;

 private:
  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<SkillLoader> skill_loader_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  const QuantClawConfig* config_ = nullptr;

  std::string get_section(const std::string& filename) const;
  std::string get_runtime_info() const;
};

}  // namespace quantclaw