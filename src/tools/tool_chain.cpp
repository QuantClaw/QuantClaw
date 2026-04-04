// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/tools/tool_chain.hpp"

#include <regex>
#include <sstream>

namespace quantclaw {

// --- ChainTemplateEngine ---

std::string ChainTemplateEngine::resolve_string(
    const std::string& tmpl,
    const std::vector<ChainStepResult>& previous_results) {
  std::string result = tmpl;

  // Replace {{prev.result}} with the last step's result
  const std::string prev_token = "{{prev.result}}";
  size_t pos = result.find(prev_token);
  while (pos != std::string::npos) {
    std::string replacement;
    if (!previous_results.empty()) {
      replacement = previous_results.back().result;
    }
    result.replace(pos, prev_token.size(), replacement);
    pos = result.find(prev_token, pos + replacement.size());
  }

  // Replace {{steps[N].result}} with the Nth step's result
  std::regex step_regex(R"(\{\{steps\[(\d+)\]\.result\}\})");
  std::smatch match;
  std::string working = result;
  result.clear();
  while (std::regex_search(working, match, step_regex)) {
    result += match.prefix().str();
    int index = std::stoi(match[1].str());
    if (index >= 0 && index < static_cast<int>(previous_results.size())) {
      result += previous_results[static_cast<size_t>(index)].result;
    }
    working = match.suffix().str();
  }
  result += working;

  return result;
}

nlohmann::json ChainTemplateEngine::resolve(
    const nlohmann::json& value,
    const std::vector<ChainStepResult>& previous_results) {
  if (value.is_string()) {
    return resolve_string(value.get<std::string>(), previous_results);
  }
  if (value.is_object()) {
    nlohmann::json resolved = nlohmann::json::object();
    for (auto& [key, val] : value.items()) {
      resolved[key] = resolve(val, previous_results);
    }
    return resolved;
  }
  if (value.is_array()) {
    nlohmann::json resolved = nlohmann::json::array();
    for (const auto& item : value) {
      resolved.push_back(resolve(item, previous_results));
    }
    return resolved;
  }
  // Numbers, booleans, null — return as-is
  return value;
}

// --- ToolChainExecutor ---

ToolChainExecutor::ToolChainExecutor(ToolExecutorFn executor,
                                     std::shared_ptr<spdlog::logger> logger)
    : executor_(std::move(executor)), logger_(std::move(logger)) {}

ChainResult ToolChainExecutor::Execute(const ToolChainDef& chain) {
  ChainResult result;
  result.chain_name = chain.name;
  result.success = true;

  for (size_t i = 0; i < chain.steps.size(); ++i) {
    const auto& step = chain.steps[i];
    ChainStepResult step_result;
    step_result.step_index = static_cast<int>(i);
    step_result.tool_name = step.tool_name;
    step_result.success = false;

    // Resolve template references
    nlohmann::json resolved_args =
        ChainTemplateEngine::resolve(step.arguments, result.step_results);

    logger_->debug("Chain '{}' step {}: executing tool '{}' with args: {}",
                   chain.name, step_result.step_index, step.tool_name,
                   resolved_args.dump());

    int attempts = 0;
    int max_attempts = (chain.error_policy == ChainErrorPolicy::kRetry)
                           ? chain.max_retries
                           : 1;

    while (attempts < max_attempts) {
      try {
        step_result.result = executor_(step.tool_name, resolved_args);
        step_result.success = true;
        step_result.error.clear();
        break;
      } catch (const std::exception& e) {
        step_result.error = e.what();
        attempts++;
        logger_->warn("Chain '{}' step {} attempt {}/{} failed: {}", chain.name,
                      step_result.step_index, attempts, max_attempts, e.what());
      }
    }

    result.step_results.push_back(step_result);

    if (!step_result.success) {
      if (chain.error_policy == ChainErrorPolicy::kStopOnError ||
          chain.error_policy == ChainErrorPolicy::kRetry) {
        result.success = false;
        result.final_result = "Chain stopped at step " +
                              std::to_string(step_result.step_index) + ": " +
                              step_result.error;
        return result;
      }
      // ContinueOnError: mark chain as failed but continue
      result.success = false;
    }
  }

  // Final result is the last successful step's result
  if (!result.step_results.empty()) {
    for (auto it = result.step_results.rbegin();
         it != result.step_results.rend(); ++it) {
      if (it->success) {
        result.final_result = it->result;
        break;
      }
    }
  }

  return result;
}

ToolChainDef ToolChainExecutor::ParseChain(const nlohmann::json& j) {
  ToolChainDef chain;
  chain.name = j.value("name", "unnamed-chain");
  chain.description = j.value("description", "");

  // Parse error policy
  std::string policy_str = j.value("error_policy", "stop_on_error");
  if (policy_str == "continue_on_error") {
    chain.error_policy = ChainErrorPolicy::kContinueOnError;
  } else if (policy_str == "retry") {
    chain.error_policy = ChainErrorPolicy::kRetry;
  } else {
    chain.error_policy = ChainErrorPolicy::kStopOnError;
  }

  chain.max_retries = j.value("max_retries", 1);

  // Parse steps
  if (j.contains("steps") && j["steps"].is_array()) {
    for (const auto& step_json : j["steps"]) {
      ChainStep step;
      step.tool_name = step_json.value("tool", "");
      step.arguments = step_json.value("arguments", nlohmann::json::object());
      chain.steps.push_back(std::move(step));
    }
  }

  return chain;
}

nlohmann::json ToolChainExecutor::ResultToJson(const ChainResult& result) {
  nlohmann::json j;
  j["chain_name"] = result.chain_name;
  j["success"] = result.success;
  j["final_result"] = result.final_result;

  nlohmann::json steps = nlohmann::json::array();
  for (const auto& sr : result.step_results) {
    nlohmann::json step_j;
    step_j["step"] = sr.step_index;
    step_j["tool"] = sr.tool_name;
    step_j["success"] = sr.success;
    step_j["result"] = sr.result;
    if (!sr.error.empty()) {
      step_j["error"] = sr.error;
    }
    steps.push_back(step_j);
  }
  j["steps"] = steps;

  return j;
}

}  // namespace quantclaw
