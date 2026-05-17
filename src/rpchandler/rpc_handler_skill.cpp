// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_skill.hpp"
#include "quantclaw/platform/process.hpp"
#include "quantclaw/skill/skill_loader_meta.hpp"
#include "quantclaw/skill/skill_loader_full.hpp"
#include "quantclaw/skill/skill_install_deps.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include <filesystem>

namespace quantclaw::gateway {

SkillHandler::SkillHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void SkillHandler::RegisterHandlers(GatewayServer& /*server*/) {
  if (!ctx_.skill_loader_meta) return;

  ctx_.server->RegisterHandler(methods::kSkillsStatus,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleSkillsStatus(params, client);
      });

  ctx_.server->RegisterHandler(methods::kSkillsInstall,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleSkillsInstall(params, client);
      });

  ctx_.server->RegisterHandler("skills.update",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleSkillsUpdate(params, client);
      });
}

nlohmann::json SkillHandler::HandleSkillsStatus(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  // --- Step 1: Resolve paths ---
  auto workspace_path =
      std::filesystem::path(quantclaw::platform::home_directory()) /
      ".quantclaw/agents/main/workspace";
  std::string managed_dir =
      (std::filesystem::path(quantclaw::platform::home_directory()) /
       ".quantclaw" / "skills")
          .string();

  // --- Step 2: Load metadata (cached or fresh) ---
  const QuantClawConfig& config = *ctx_.config;
  auto skills_meta = ctx_.skill_loader_meta->IsLoadAllMetaData()
                        ? ctx_.skill_loader_meta->GetAllMetaData()
                        : ctx_.skill_loader_meta->LoaderAllMetaData(config.skills, workspace_path.string());

  // --- Step 3: Build skill entries ---
  SkillLoaderFull skill_loader_full(ctx_.logger);
  nlohmann::json skill_entries = nlohmann::json::array();

  for (const auto& skill_meta : skills_meta) {
    // Gating check
    SkillFullData skill_full_data = skill_loader_full.LoaderOneFullData(skill_meta);
    bool gated = !skill_loader_full.CheckSkillGating(skill_full_data);

    // Resolve skill key
    std::string skill_key = skill_meta.extra.skill_key.empty()
                                ? skill_meta.mini.name
                                : skill_meta.extra.skill_key;

    // Build install options
    nlohmann::json install_opts = nlohmann::json::array();
    for (const auto& inst : skill_meta.extra.installs) {
      std::string kind = inst.EffectiveMethod();
      if (kind == "node" || kind == "go" || kind == "uv" || kind == "brew") {
        nlohmann::json opt;
        opt["id"] = kind + ":" + inst.EffectiveFormula();
        opt["kind"] = kind;
        opt["label"] = inst.label.empty() ? inst.EffectiveFormula() : inst.label;
        nlohmann::json bins = nlohmann::json::array();
        for (const auto& b : inst.bins) bins.push_back(b);
        if (bins.empty() && !inst.EffectiveBinary().empty()) {
          bins.push_back(inst.EffectiveBinary());
        }
        opt["bins"] = bins;
        install_opts.push_back(opt);
      }
    }

    // Build requirements
    nlohmann::json required_bins = nlohmann::json::array();
    for (const auto& b : skill_meta.extra.required_bins) required_bins.push_back(b);
    nlohmann::json required_envs = nlohmann::json::array();
    for (const auto& e : skill_meta.extra.required_envs) required_envs.push_back(e);
    nlohmann::json os_restrict = nlohmann::json::array();
    for (const auto& o : skill_meta.mini.os_restrict) os_restrict.push_back(o);

    // Compose skill entry
    skill_entries.push_back({
        {"name", skill_meta.mini.name},
        {"description", skill_meta.mini.description},
        {"source", "bundled"},
        {"filePath", skill_meta.mini.root_dir},
        {"baseDir", skill_meta.mini.root_dir},
        {"skillKey", skill_key},
        {"bundled", true},
        {"primaryEnv", skill_meta.extra.primary_env},
        {"emoji", skill_meta.mini.emoji},
        {"homepage", skill_meta.extra.homepage},
        {"always", skill_meta.mini.always},
        {"disabled", false},
        {"blockedByAllowlist", false},
        {"eligible", !gated},
        {"requirements",
         {{"bins", required_bins},
          {"env", required_envs},
          {"config", nlohmann::json::array()},
          {"os", os_restrict}}},
        {"missing",
         {{"bins", nlohmann::json::array()},
          {"env", nlohmann::json::array()},
          {"config", nlohmann::json::array()},
          {"os", nlohmann::json::array()}}},
        {"configChecks", nlohmann::json::array()},
        {"install", install_opts}});
  }

  // --- Step 4: Return result ---
  return {{"workspaceDir", workspace_path.string()},
          {"managedSkillsDir", managed_dir},
          {"skills", skill_entries}};
}

nlohmann::json SkillHandler::HandleSkillsInstall(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string name = params.value("name", "");
  if (name.empty()) {
    throw std::runtime_error("skill name is required");
  }

  quantclaw::SkillMetadata skill_meta_data;
  skill_meta_data.mini.name = name;
  skill_meta_data.mini.root_dir = params.value("rootDir", "");
  quantclaw::SkillInstallDeps skill_install_deps(ctx_.logger);
  bool ok = skill_install_deps.InstallSkillDependenceByName(name, {skill_meta_data});

  return {{"ok", ok}, {"name", name}};
}

nlohmann::json SkillHandler::HandleSkillsUpdate(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string skill_key = params.value("skillKey", "");
  if (skill_key.empty()) {
    throw std::runtime_error("skillKey is required");
  }

  std::string config_path = QuantClawConfig::DefaultConfigPath();
  bool has_enabled = params.contains("enabled") && params["enabled"].is_boolean();
  bool has_api_key = params.contains("apiKey") && params["apiKey"].is_string();

  if (!has_enabled && !has_api_key) {
    throw std::runtime_error("'enabled' or 'apiKey' is required");
  }

  // --- Update enabled state ---
  if (has_enabled) {
    bool enabled = params["enabled"].get<bool>();
    QuantClawConfig::SetValue(config_path,
        "skills.entries." + skill_key + ".enabled", enabled);

    // Update in-memory skill metadata cache
    if (!enabled) {
      // Resolve skill name from skillKey for RemoveOneMetaData
      auto meta_opt = ctx_.skill_loader_meta->FindMetaDataByKey(skill_key);
      std::string name = meta_opt ? meta_opt->mini.name : skill_key;
      ctx_.skill_loader_meta->RemoveOneMetaData(name);
    } else {
      // Re-add metadata if previously removed: scan by key
      auto meta_opt = ctx_.skill_loader_meta->FindMetaDataByKey(skill_key);
      if (!meta_opt) {
        // Skill was removed from cache; reload will repopulate
        ctx_.logger->info("HandleSkillsUpdate: skill '{}' re-enabled, "
                          "will be repopulated on reload", skill_key);
      }
    }

    ctx_.logger->info("HandleSkillsUpdate: skill '{}' {}",
                      skill_key, enabled ? "enabled" : "disabled");
  }

  // --- Update API key ---
  if (has_api_key) {
    std::string api_key = params["apiKey"].get<std::string>();
    QuantClawConfig::SetValue(config_path,
        "skills.configs." + skill_key + ".apiKey", api_key);

    ctx_.logger->info("HandleSkillsUpdate: API key updated for skill '{}'",
                      skill_key);
  }

  // Propagate config changes to runtime components
  if (ctx_.reload_fn) {
    ctx_.reload_fn();
  }

  return {{"ok", true}, {"skillKey", skill_key}};
}

}  // namespace quantclaw::gateway
