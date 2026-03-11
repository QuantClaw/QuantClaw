// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/tools/tool_chain.hpp"

#include <gtest/gtest.h>

class ToolChainTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);
  }

  std::shared_ptr<spdlog::logger> logger_;

  // Simple executor that echoes arguments or returns fixed values
  quantclaw::ToolExecutorFn echo_executor() {
    return [](const std::string& tool_name,
              const nlohmann::json& args) -> std::string {
      if (tool_name == "echo") {
        return args.value("text", "");
      }
      if (tool_name == "upper") {
        std::string input = args.value("text", "");
        std::string result;
        for (char c : input)
          result += static_cast<char>(std::toupper(c));
        return result;
      }
      if (tool_name == "concat") {
        return args.value("a", "") + args.value("b", "");
      }
      if (tool_name == "fail") {
        throw std::runtime_error("intentional failure");
      }
      throw std::runtime_error("Unknown tool: " + tool_name);
    };
  }
};

// --- Template Engine Tests ---

TEST_F(ToolChainTest, TemplateResolvePrevResult) {
  std::vector<quantclaw::ChainStepResult> results = {
      {0, "echo", "hello", "", true}};

  nlohmann::json input = {{"text", "{{prev.result}}"}};
  auto resolved = quantclaw::ChainTemplateEngine::resolve(input, results);
  EXPECT_EQ(resolved["text"].get<std::string>(), "hello");
}

TEST_F(ToolChainTest, TemplateResolveStepIndex) {
  std::vector<quantclaw::ChainStepResult> results = {
      {0, "echo", "first", "", true}, {1, "echo", "second", "", true}};

  nlohmann::json input = {{"text", "{{steps[0].result}}"}};
  auto resolved = quantclaw::ChainTemplateEngine::resolve(input, results);
  EXPECT_EQ(resolved["text"].get<std::string>(), "first");

  input = {{"text", "{{steps[1].result}}"}};
  resolved = quantclaw::ChainTemplateEngine::resolve(input, results);
  EXPECT_EQ(resolved["text"].get<std::string>(), "second");
}

TEST_F(ToolChainTest, TemplateResolveNestedObject) {
  std::vector<quantclaw::ChainStepResult> results = {
      {0, "echo", "world", "", true}};

  nlohmann::json input = {{"outer", {{"inner", "hello {{prev.result}}"}}}};
  auto resolved = quantclaw::ChainTemplateEngine::resolve(input, results);
  EXPECT_EQ(resolved["outer"]["inner"].get<std::string>(), "hello world");
}

TEST_F(ToolChainTest, TemplateResolveArray) {
  std::vector<quantclaw::ChainStepResult> results = {
      {0, "echo", "item", "", true}};

  nlohmann::json input = nlohmann::json::array({"{{prev.result}}", "static"});
  auto resolved = quantclaw::ChainTemplateEngine::resolve(input, results);
  EXPECT_EQ(resolved[0].get<std::string>(), "item");
  EXPECT_EQ(resolved[1].get<std::string>(), "static");
}

TEST_F(ToolChainTest, TemplatePreservesNonStringValues) {
  std::vector<quantclaw::ChainStepResult> results;

  nlohmann::json input = {{"count", 42}, {"flag", true}, {"empty", nullptr}};
  auto resolved = quantclaw::ChainTemplateEngine::resolve(input, results);
  EXPECT_EQ(resolved["count"].get<int>(), 42);
  EXPECT_EQ(resolved["flag"].get<bool>(), true);
  EXPECT_TRUE(resolved["empty"].is_null());
}

TEST_F(ToolChainTest, TemplateEmptyPrevResult) {
  std::vector<quantclaw::ChainStepResult> results;  // empty
  nlohmann::json input = {{"text", "prefix-{{prev.result}}-suffix"}};
  auto resolved = quantclaw::ChainTemplateEngine::resolve(input, results);
  EXPECT_EQ(resolved["text"].get<std::string>(), "prefix--suffix");
}

// --- Chain Executor Tests ---

TEST_F(ToolChainTest, SimpleChainExecution) {
  quantclaw::ToolChainDef chain;
  chain.name = "simple";
  chain.steps = {{"echo", {{"text", "hello"}}},
                 {"upper", {{"text", "{{prev.result}}"}}}};

  quantclaw::ToolChainExecutor executor(echo_executor(), logger_);
  auto result = executor.Execute(chain);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.step_results.size(), 2u);
  EXPECT_EQ(result.step_results[0].result, "hello");
  EXPECT_EQ(result.step_results[1].result, "HELLO");
  EXPECT_EQ(result.final_result, "HELLO");
}

TEST_F(ToolChainTest, ChainWithStepIndexReference) {
  quantclaw::ToolChainDef chain;
  chain.name = "indexed";
  chain.steps = {
      {"echo", {{"text", "first"}}},
      {"echo", {{"text", "second"}}},
      {"concat", {{"a", "{{steps[0].result}}"}, {"b", "{{steps[1].result}}"}}}};

  quantclaw::ToolChainExecutor executor(echo_executor(), logger_);
  auto result = executor.Execute(chain);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.final_result, "firstsecond");
}

TEST_F(ToolChainTest, ChainStopOnError) {
  quantclaw::ToolChainDef chain;
  chain.name = "stop-chain";
  chain.error_policy = quantclaw::ChainErrorPolicy::kStopOnError;
  chain.steps = {{"echo", {{"text", "ok"}}},
                 {"fail", {}},
                 {"echo", {{"text", "never reached"}}}};

  quantclaw::ToolChainExecutor executor(echo_executor(), logger_);
  auto result = executor.Execute(chain);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.step_results.size(), 2u);  // Stopped after step 1
  EXPECT_TRUE(result.step_results[0].success);
  EXPECT_FALSE(result.step_results[1].success);
}

TEST_F(ToolChainTest, ChainContinueOnError) {
  quantclaw::ToolChainDef chain;
  chain.name = "continue-chain";
  chain.error_policy = quantclaw::ChainErrorPolicy::kContinueOnError;
  chain.steps = {{"echo", {{"text", "first"}}},
                 {"fail", {}},
                 {"echo", {{"text", "third"}}}};

  quantclaw::ToolChainExecutor executor(echo_executor(), logger_);
  auto result = executor.Execute(chain);

  EXPECT_FALSE(result.success);               // Overall failure due to step 1
  EXPECT_EQ(result.step_results.size(), 3u);  // All steps ran
  EXPECT_TRUE(result.step_results[0].success);
  EXPECT_FALSE(result.step_results[1].success);
  EXPECT_TRUE(result.step_results[2].success);
  EXPECT_EQ(result.final_result, "third");
}

TEST_F(ToolChainTest, ChainRetryPolicy) {
  int call_count = 0;
  quantclaw::ToolExecutorFn flaky_executor =
      [&call_count](const std::string& /*tool_name*/,
                    const nlohmann::json& /*args*/) -> std::string {
    call_count++;
    if (call_count < 3) {
      throw std::runtime_error("transient error");
    }
    return "success after retries";
  };

  quantclaw::ToolChainDef chain;
  chain.name = "retry-chain";
  chain.error_policy = quantclaw::ChainErrorPolicy::kRetry;
  chain.max_retries = 3;
  chain.steps = {{"flaky", {}}};

  quantclaw::ToolChainExecutor executor(flaky_executor, logger_);
  auto result = executor.Execute(chain);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.final_result, "success after retries");
  EXPECT_EQ(call_count, 3);
}

TEST_F(ToolChainTest, EmptyChain) {
  quantclaw::ToolChainDef chain;
  chain.name = "empty";

  quantclaw::ToolChainExecutor executor(echo_executor(), logger_);
  auto result = executor.Execute(chain);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.step_results.empty());
  EXPECT_TRUE(result.final_result.empty());
}

// --- Parse / Serialize Tests ---

TEST_F(ToolChainTest, ParseChainFromJson) {
  nlohmann::json j = {
      {"name", "test-chain"},
      {"description", "A test chain"},
      {"error_policy", "continue_on_error"},
      {"max_retries", 2},
      {"steps",
       {{{"tool", "read"}, {"arguments", {{"path", "/tmp/test.txt"}}}},
        {{"tool", "write"},
         {"arguments",
          {{"path", "/tmp/out.txt"}, {"content", "{{prev.result}}"}}}}}}};

  auto chain = quantclaw::ToolChainExecutor::ParseChain(j);
  EXPECT_EQ(chain.name, "test-chain");
  EXPECT_EQ(chain.description, "A test chain");
  EXPECT_EQ(chain.error_policy, quantclaw::ChainErrorPolicy::kContinueOnError);
  EXPECT_EQ(chain.max_retries, 2);
  EXPECT_EQ(chain.steps.size(), 2u);
  EXPECT_EQ(chain.steps[0].tool_name, "read");
  EXPECT_EQ(chain.steps[1].tool_name, "write");
}

TEST_F(ToolChainTest, ResultToJson) {
  quantclaw::ChainResult result;
  result.chain_name = "test";
  result.success = true;
  result.final_result = "done";
  result.step_results = {{0, "echo", "hello", "", true},
                         {1, "upper", "HELLO", "", true}};

  auto j = quantclaw::ToolChainExecutor::ResultToJson(result);
  EXPECT_EQ(j["chain_name"], "test");
  EXPECT_TRUE(j["success"].get<bool>());
  EXPECT_EQ(j["final_result"], "done");
  EXPECT_EQ(j["steps"].size(), 2u);
  EXPECT_EQ(j["steps"][0]["tool"], "echo");
  EXPECT_TRUE(j["steps"][0]["success"].get<bool>());
}

TEST_F(ToolChainTest, ParseDefaultValues) {
  nlohmann::json j = {{"steps", {{{"tool", "read"}}}}};
  auto chain = quantclaw::ToolChainExecutor::ParseChain(j);
  EXPECT_EQ(chain.name, "unnamed-chain");
  EXPECT_EQ(chain.error_policy, quantclaw::ChainErrorPolicy::kStopOnError);
  EXPECT_EQ(chain.max_retries, 1);
}
