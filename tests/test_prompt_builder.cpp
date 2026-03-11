// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class PromptBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_prompt_test");
    std::filesystem::create_directories(test_dir_ / "skills");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_prompt", null_sink);

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(test_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    builder_ = std::make_unique<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);
  }

  void TearDown() override {
    builder_.reset();
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  void write_file(const std::string& name, const std::string& content) {
    std::ofstream f(test_dir_ / name);
    f << content;
    f.close();
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
  std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
  std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
  std::unique_ptr<quantclaw::PromptBuilder> builder_;
};

// --- BuildFull tests ---

TEST_F(PromptBuilderTest, BuildFullContainsDefaultIdentity) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("You are QuantClaw"), std::string::npos);
  EXPECT_NE(prompt.find("personal AI assistant"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesSoulSection) {
  write_file("SOUL.md", "I am a quantum trading bot.");
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Your Identity"), std::string::npos);
  EXPECT_NE(prompt.find("I am a quantum trading bot."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesAgentsSection) {
  write_file("AGENTS.md", "Always be concise and direct.");
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Agent Behavior"), std::string::npos);
  EXPECT_NE(prompt.find("Always be concise and direct."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesToolsSection) {
  write_file("TOOLS.md", "Use read for file operations.");
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Tool Usage Guide"), std::string::npos);
  EXPECT_NE(prompt.find("Use read for file operations."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesMemorySection) {
  write_file("MEMORY.md", "User prefers short answers.");
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Memory"), std::string::npos);
  EXPECT_NE(prompt.find("User prefers short answers."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesRuntimeInfo) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Runtime Information"), std::string::npos);
  EXPECT_NE(prompt.find("Current time:"), std::string::npos);
  EXPECT_NE(prompt.find("Workspace:"), std::string::npos);
  EXPECT_NE(prompt.find("Platform:"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullIncludesToolSchemas) {
  auto prompt = builder_->BuildFull();
  EXPECT_NE(prompt.find("## Available Tools"), std::string::npos);
  // Built-in tools should be listed
  EXPECT_NE(prompt.find("read"), std::string::npos);
  EXPECT_NE(prompt.find("write"), std::string::npos);
  EXPECT_NE(prompt.find("exec"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildFullOmitsMissingSections) {
  // No SOUL.md, AGENTS.md, TOOLS.md, MEMORY.md exist
  auto prompt = builder_->BuildFull();
  EXPECT_EQ(prompt.find("## Your Identity"), std::string::npos);
  EXPECT_EQ(prompt.find("## Agent Behavior"), std::string::npos);
  EXPECT_EQ(prompt.find("## Tool Usage Guide"), std::string::npos);
  EXPECT_EQ(prompt.find("## Memory"), std::string::npos);
  // Runtime info and tools should still be present
  EXPECT_NE(prompt.find("## Runtime Information"), std::string::npos);
  EXPECT_NE(prompt.find("## Available Tools"), std::string::npos);
}

// --- BuildMinimal tests ---

TEST_F(PromptBuilderTest, BuildMinimalContainsIdentityFallback) {
  auto prompt = builder_->BuildMinimal();
  EXPECT_NE(prompt.find("You are QuantClaw"), std::string::npos);
  EXPECT_NE(prompt.find("helpful AI assistant"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildMinimalIncludesSoulButNotAgents) {
  write_file("SOUL.md", "I am a quantum bot.");
  write_file("AGENTS.md", "Be verbose.");
  auto prompt = builder_->BuildMinimal();
  EXPECT_NE(prompt.find("I am a quantum bot."), std::string::npos);
  // Minimal should NOT include agents section
  EXPECT_EQ(prompt.find("## Agent Behavior"), std::string::npos);
  EXPECT_EQ(prompt.find("Be verbose."), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildMinimalIncludesTools) {
  auto prompt = builder_->BuildMinimal();
  EXPECT_NE(prompt.find("## Available Tools"), std::string::npos);
  EXPECT_NE(prompt.find("read"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildMinimalNoRuntimeInfo) {
  auto prompt = builder_->BuildMinimal();
  EXPECT_EQ(prompt.find("## Runtime Information"), std::string::npos);
}

// --- BuildFull with all sections populated ---

TEST_F(PromptBuilderTest, BuildFullWithAllSections) {
  write_file("SOUL.md", "SOUL_CONTENT");
  write_file("AGENTS.md", "AGENTS_CONTENT");
  write_file("TOOLS.md", "TOOLS_CONTENT");
  write_file("MEMORY.md", "MEMORY_CONTENT");

  auto prompt = builder_->BuildFull();

  // Verify section order: Identity before Agent Behavior before Tool Usage
  auto soul_pos = prompt.find("## Your Identity");
  auto agents_pos = prompt.find("## Agent Behavior");
  auto tools_guide_pos = prompt.find("## Tool Usage Guide");
  auto memory_pos = prompt.find("## Memory");
  auto runtime_pos = prompt.find("## Runtime Information");
  auto tools_pos = prompt.find("## Available Tools");

  ASSERT_NE(soul_pos, std::string::npos);
  ASSERT_NE(agents_pos, std::string::npos);
  ASSERT_NE(tools_guide_pos, std::string::npos);
  ASSERT_NE(memory_pos, std::string::npos);
  ASSERT_NE(runtime_pos, std::string::npos);
  ASSERT_NE(tools_pos, std::string::npos);

  EXPECT_LT(soul_pos, agents_pos);
  EXPECT_LT(agents_pos, tools_guide_pos);
  EXPECT_LT(tools_guide_pos, memory_pos);
  EXPECT_LT(memory_pos, runtime_pos);
  EXPECT_LT(runtime_pos, tools_pos);
}

// --- No tools registered ---

TEST_F(PromptBuilderTest, BuildFullNoToolsRegistered) {
  // Create a fresh registry without built-in tools
  auto empty_registry = std::make_shared<quantclaw::ToolRegistry>(logger_);
  auto builder =
      quantclaw::PromptBuilder(memory_manager_, skill_loader_, empty_registry);

  auto prompt = builder.BuildFull();
  EXPECT_EQ(prompt.find("## Available Tools"), std::string::npos);
  // Default identity should still be there
  EXPECT_NE(prompt.find("You are QuantClaw"), std::string::npos);
}
