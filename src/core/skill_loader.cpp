// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/skill_loader.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stack>
#include <unordered_set>

#include <spdlog/spdlog.h>

namespace quantclaw {

SkillLoader::SkillLoader(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
  logger_->info("SkillLoader initialized");
}

std::vector<SkillMetadata>
SkillLoader::LoadSkillsFromDirectory(const std::filesystem::path& skills_dir) {
  std::vector<SkillMetadata> skills;

  if (!std::filesystem::exists(skills_dir)) {
    logger_->debug("Skills directory does not exist: {}", skills_dir.string());
    return skills;
  }

  logger_->info("Loading skills from: {}", skills_dir.string());

  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(skills_dir)) {
    if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
      try {
        auto skill = parse_skill_file(entry.path());
        if (CheckSkillGating(skill)) {
          logger_->debug("Loaded skill: {}", skill.name);
          skills.push_back(std::move(skill));
        } else {
          logger_->debug("Skipped skill (gating failed): {}", skill.name);
        }
      } catch (const std::exception& e) {
        logger_->error("Failed to load skill from {}: {}",
                       entry.path().string(), e.what());
      }
    }
  }

  logger_->info("Loaded {} skills", skills.size());
  return skills;
}

bool SkillLoader::CheckSkillGating(const SkillMetadata& skill) {
  // If always=true, skip all gating
  if (skill.always) {
    return true;
  }

  // Check OS restriction
  if (!skill.os_restrict.empty()) {
    if (!check_os_restriction(skill.os_restrict)) {
      logger_->debug("Skill gating failed: OS not in allowed list");
      return false;
    }
  }

  // Check required binaries (all must exist)
  for (const auto& binary : skill.required_bins) {
    if (!is_binary_available(binary)) {
      logger_->debug("Skill gating failed: binary '{}' not available", binary);
      return false;
    }
  }

  // Check anyBins (at least one must exist)
  if (!skill.any_bins.empty()) {
    bool found = false;
    for (const auto& binary : skill.any_bins) {
      if (is_binary_available(binary)) {
        found = true;
        break;
      }
    }
    if (!found) {
      logger_->debug("Skill gating failed: none of anyBins available");
      return false;
    }
  }

  // Check required environment variables
  for (const auto& env_var : skill.required_envs) {
    if (!is_env_var_available(env_var)) {
      logger_->debug("Skill gating failed: env '{}' not available", env_var);
      return false;
    }
  }

  // Check required config files
  for (const auto& config_file : skill.config_files) {
    std::string expanded = config_file;
    if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
      const char* home = std::getenv("HOME");
      if (home) {
        expanded = std::string(home) + expanded.substr(1);
      }
    }
    if (!std::filesystem::exists(expanded)) {
      logger_->debug("Skill gating failed: config '{}' not found", config_file);
      return false;
    }
  }

  return true;
}

std::string
SkillLoader::GetSkillContext(const std::vector<SkillMetadata>& skills) const {
  std::ostringstream context;

  for (const auto& skill : skills) {
    if (!skill.emoji.empty()) {
      context << skill.emoji << " ";
    }
    context << "### " << skill.name << "\n";
    if (!skill.description.empty()) {
      context << skill.description << "\n\n";
    }
    context << skill.content << "\n";

    // Append resource directory info if available
    bool has_resources = false;
    if (!skill.scripts_dir.empty() || !skill.references_dir.empty() ||
        !skill.assets_dir.empty()) {
      context << "\n**Resources:**\n";
      has_resources = true;
    }
    if (!skill.scripts_dir.empty()) {
      context << "- Scripts: `" << skill.scripts_dir << "`\n";
    }
    if (!skill.references_dir.empty()) {
      context << "- References: `" << skill.references_dir << "`\n";
    }
    if (!skill.assets_dir.empty()) {
      context << "- Assets: `" << skill.assets_dir << "`\n";
    }

    // List slash commands
    if (!skill.commands.empty()) {
      if (!has_resources)
        context << "\n";
      context << "**Commands:**\n";
      for (const auto& cmd : skill.commands) {
        context << "- `/" << cmd.name << "`";
        if (!cmd.description.empty()) {
          context << " — " << cmd.description;
        }
        context << "\n";
      }
    }

    context << "\n";
  }

  return context.str();
}

bool SkillLoader::InstallSkill(const SkillMetadata& skill) {
  if (skill.installs.empty()) {
    logger_->info("Skill '{}' has no install instructions", skill.name);
    return true;
  }

  bool all_ok = true;
  for (const auto& inst : skill.installs) {
    std::string eff_binary = inst.EffectiveBinary();
    std::string eff_method = inst.EffectiveMethod();
    std::string eff_formula = inst.EffectiveFormula();

    // Skip if binary already available
    if (!eff_binary.empty() && is_binary_available(eff_binary)) {
      logger_->debug("Skill '{}': {} already installed", skill.name,
                     eff_binary);
      continue;
    }

    std::string cmd;
    if (eff_method == "node") {
      cmd = "npm install -g " + eff_formula;
    } else if (eff_method == "go") {
      cmd = "go install " + eff_formula;
    } else if (eff_method == "uv") {
      cmd = "uv pip install " + eff_formula;
    } else if (eff_method == "apt") {
      cmd = "sudo apt-get install -y " + eff_formula;
    } else if (eff_method == "brew") {
      cmd = "brew install " + eff_formula;
    } else if (eff_method == "download") {
      const char* home = std::getenv("HOME");
      std::string bin_dir =
          std::string(home ? home : "/tmp") + "/.quantclaw/bin";
      std::filesystem::create_directories(bin_dir);
      std::string dest =
          bin_dir + "/" + (eff_binary.empty() ? "downloaded" : eff_binary);
      cmd =
          "curl -fsSL -o " + dest + " " + eff_formula + " && chmod +x " + dest;
    } else {
      logger_->warn("Skill '{}': unknown install method '{}'", skill.name,
                    eff_method);
      continue;
    }

    logger_->info("Installing skill '{}' via {}: {}", skill.name, eff_method,
                  cmd);
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
      logger_->error("Skill '{}' install failed (exit {})", skill.name, ret);
      all_ok = false;
    }
  }
  return all_ok;
}

std::vector<SkillCommand>
SkillLoader::GetAllCommands(const std::vector<SkillMetadata>& skills) const {
  std::vector<SkillCommand> commands;
  for (const auto& skill : skills) {
    for (const auto& cmd : skill.commands) {
      commands.push_back(cmd);
    }
  }
  return commands;
}

SkillMetadata
SkillLoader::parse_skill_file(const std::filesystem::path& skill_file) const {
  std::ifstream file(skill_file);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open skill file: " +
                             skill_file.string());
  }

  std::ostringstream content;
  content << file.rdbuf();
  file.close();

  std::string file_content = content.str();
  SkillMetadata skill;

  // Extract frontmatter (YAML between --- markers)
  std::regex frontmatter_regex(R"(^---\s*\n([\s\S]*?)\n---\s*\n)");
  std::smatch matches;

  if (std::regex_search(file_content, matches, frontmatter_regex)) {
    std::string frontmatter = matches[1].str();

    try {
      nlohmann::json metadata = parse_yaml_frontmatter(frontmatter);

      // Extract basic fields
      if (metadata.contains("name")) {
        skill.name = metadata["name"].get<std::string>();
      }
      if (metadata.contains("description")) {
        skill.description = metadata["description"].get<std::string>();
      }
      if (metadata.contains("emoji")) {
        skill.emoji = metadata["emoji"].get<std::string>();
      }
      if (metadata.contains("primaryEnv")) {
        skill.primary_env = metadata["primaryEnv"].get<std::string>();
      }
      if (metadata.contains("always")) {
        if (metadata["always"].is_boolean()) {
          skill.always = metadata["always"].get<bool>();
        } else {
          skill.always = metadata["always"].get<std::string>() == "true";
        }
      }
      if (metadata.contains("os")) {
        if (metadata["os"].is_array()) {
          skill.os_restrict = metadata["os"].get<std::vector<std::string>>();
        }
      }

      // Extract requires section (flat or nested)
      auto extract_requires = [&](const nlohmann::json& reqs) {
        if (reqs.contains("bins")) {
          skill.required_bins = reqs["bins"].get<std::vector<std::string>>();
        }
        if (reqs.contains("env")) {
          skill.required_envs = reqs["env"].get<std::vector<std::string>>();
        }
        if (reqs.contains("envs")) {
          skill.required_envs = reqs["envs"].get<std::vector<std::string>>();
        }
        if (reqs.contains("anyBins")) {
          skill.any_bins = reqs["anyBins"].get<std::vector<std::string>>();
        }
        if (reqs.contains("config")) {
          skill.config_files = reqs["config"].get<std::vector<std::string>>();
        }
      };

      if (metadata.contains("requires") && metadata["requires"].is_object()) {
        extract_requires(metadata["requires"]);
      }
      // OpenClaw nested format
      if (metadata.contains("metadata") &&
          metadata["metadata"].contains("openclaw") &&
          metadata["metadata"]["openclaw"].contains("requires")) {
        extract_requires(metadata["metadata"]["openclaw"]["requires"]);
      }

      // Fallback: simple YAML parser flattens nested requires.X into
      // top-level keys — check those too
      if (skill.required_envs.empty() && metadata.contains("env") &&
          metadata["env"].is_array()) {
        skill.required_envs = metadata["env"].get<std::vector<std::string>>();
      }
      if (skill.required_bins.empty() && metadata.contains("bins") &&
          metadata["bins"].is_array()) {
        skill.required_bins = metadata["bins"].get<std::vector<std::string>>();
      }
      if (skill.any_bins.empty() && metadata.contains("anyBins") &&
          metadata["anyBins"].is_array()) {
        skill.any_bins = metadata["anyBins"].get<std::vector<std::string>>();
      }
      if (skill.config_files.empty() && metadata.contains("config") &&
          metadata["config"].is_array()) {
        skill.config_files = metadata["config"].get<std::vector<std::string>>();
      }

      // Extract additional metadata.openclaw fields
      if (metadata.contains("metadata") &&
          metadata["metadata"].contains("openclaw")) {
        const auto& oc = metadata["metadata"]["openclaw"];
        if (oc.contains("emoji") && skill.emoji.empty()) {
          skill.emoji = oc["emoji"].get<std::string>();
        }
        if (oc.contains("primaryEnv") && skill.primary_env.empty()) {
          skill.primary_env = oc["primaryEnv"].get<std::string>();
        }
        if (oc.contains("homepage")) {
          skill.homepage = oc["homepage"].get<std::string>();
        }
        if (oc.contains("skillKey")) {
          skill.skill_key = oc["skillKey"].get<std::string>();
        }
        if (oc.contains("always") && !skill.always) {
          if (oc["always"].is_boolean()) {
            skill.always = oc["always"].get<bool>();
          }
        }
        if (oc.contains("os") && skill.os_restrict.empty()) {
          if (oc["os"].is_array()) {
            skill.os_restrict = oc["os"].get<std::vector<std::string>>();
          }
        }
      }
      if (metadata.contains("homepage") && skill.homepage.empty()) {
        skill.homepage = metadata["homepage"].get<std::string>();
      }
      if (metadata.contains("skillKey") && skill.skill_key.empty()) {
        skill.skill_key = metadata["skillKey"].get<std::string>();
      }

      // Extract install info — supports both formats:
      //   QuantClaw object: { "apt": "curl", "node": "@pkg/cli" }
      //   OpenClaw array:   [{ "kind": "brew", "formula": "curl", "bins":
      //   ["curl"] }]
      nlohmann::json* install_section = nullptr;
      if (metadata.contains("metadata") &&
          metadata["metadata"].contains("openclaw") &&
          metadata["metadata"]["openclaw"].contains("install")) {
        install_section = &metadata["metadata"]["openclaw"]["install"];
      } else if (metadata.contains("install")) {
        install_section = &metadata["install"];
      }
      if (install_section) {
        if (install_section->is_array()) {
          // OpenClaw array format
          for (const auto& entry : *install_section) {
            if (!entry.is_object())
              continue;
            SkillInstallInfo info;
            info.kind = entry.value("kind", "");
            info.method = info.kind;  // alias
            info.id = entry.value("id", "");
            info.label = entry.value("label", "");
            info.formula = entry.value("formula", "");
            info.package = entry.value("package", "");
            info.module = entry.value("module", "");
            info.url = entry.value("url", "");
            info.archive = entry.value("archive", "");
            info.target_dir = entry.value("targetDir", "");
            info.extract = entry.value("extract", false);
            info.strip_components = entry.value("stripComponents", 0);
            if (entry.contains("bins") && entry["bins"].is_array()) {
              info.bins = entry["bins"].get<std::vector<std::string>>();
              if (info.binary.empty() && !info.bins.empty()) {
                info.binary = info.bins.front();
              }
            }
            if (entry.contains("os") && entry["os"].is_array()) {
              info.os = entry["os"].get<std::vector<std::string>>();
            }
            skill.installs.push_back(std::move(info));
          }
        } else if (install_section->is_object()) {
          // QuantClaw object format: { method: formula }
          for (auto it = install_section->begin(); it != install_section->end();
               ++it) {
            SkillInstallInfo info;
            info.method = it.key();
            info.kind = it.key();
            if (it.value().is_string()) {
              info.formula = it.value().get<std::string>();
            } else if (it.value().is_object()) {
              info.formula = it.value().value("formula", "");
              info.binary = it.value().value("binary", "");
              info.package = it.value().value("package", "");
              info.url = it.value().value("url", "");
              if (it.value().contains("bins") &&
                  it.value()["bins"].is_array()) {
                info.bins = it.value()["bins"].get<std::vector<std::string>>();
              }
            }
            skill.installs.push_back(std::move(info));
          }
        }
      }

      // Extract slash commands
      nlohmann::json* cmds_section = nullptr;
      if (metadata.contains("commands") && metadata["commands"].is_array()) {
        cmds_section = &metadata["commands"];
      } else if (metadata.contains("metadata") &&
                 metadata["metadata"].contains("openclaw") &&
                 metadata["metadata"]["openclaw"].contains("commands") &&
                 metadata["metadata"]["openclaw"]["commands"].is_array()) {
        cmds_section = &metadata["metadata"]["openclaw"]["commands"];
      }
      if (cmds_section) {
        for (const auto& cmd : *cmds_section) {
          if (!cmd.is_object())
            continue;
          SkillCommand sc;
          sc.name = cmd.value("name", "");
          sc.description = cmd.value("description", "");
          sc.tool_name = cmd.value("toolName", "");
          sc.arg_mode = cmd.value("argMode", "freeform");
          if (!sc.name.empty()) {
            skill.commands.push_back(std::move(sc));
          }
        }
      }

    } catch (const std::exception& e) {
      logger_->warn("Failed to parse skill frontmatter: {}", e.what());
    }
  }

  // Default name from directory
  if (skill.name.empty()) {
    skill.name = skill_file.parent_path().filename().string();
  }

  // Set root directory and detect resource subdirectories
  skill.root_dir = skill_file.parent_path();
  auto check_resource_dir = [&](const std::string& subdir) -> std::string {
    auto p = skill.root_dir / subdir;
    if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
      return p.string();
    }
    return "";
  };
  skill.scripts_dir = check_resource_dir("scripts");
  skill.references_dir = check_resource_dir("references");
  skill.assets_dir = check_resource_dir("assets");

  // Content is everything after frontmatter
  std::string content_part = file_content;
  if (!matches.empty()) {
    content_part = file_content.substr(matches[0].length());
  }
  skill.content = content_part;

  return skill;
}

bool SkillLoader::is_binary_available(const std::string& binary_name) const {
#ifdef _WIN32
  std::string command = "where " + binary_name + " > nul 2>&1";
#else
  std::string command = "which " + binary_name + " > /dev/null 2>&1";
#endif
  int result = std::system(command.c_str());
  return result == 0;
}

bool SkillLoader::is_env_var_available(const std::string& env_var) const {
  const char* value = std::getenv(env_var.c_str());
  return value != nullptr && std::strlen(value) > 0;
}

bool SkillLoader::check_os_restriction(
    const std::vector<std::string>& os_list) const {
  std::string current = get_current_os();
  for (const auto& os : os_list) {
    std::string normalized = os;
    if (normalized == "macos")
      normalized = "darwin";
    if (normalized == current)
      return true;
  }
  return false;
}

std::string SkillLoader::get_current_os() const {
#ifdef __linux__
  return "linux";
#elif defined(__APPLE__)
  return "darwin";
#elif defined(_WIN32)
  return "win32";
#else
  return "unknown";
#endif
}

nlohmann::json
SkillLoader::parse_yaml_frontmatter(const std::string& yaml_str) const {
  nlohmann::json root = nlohmann::json::object();

  // Stack: (indent_level, pointer to current json object)
  std::stack<std::pair<int, nlohmann::json*>> ctx;
  ctx.push({-1, &root});

  std::istringstream stream(yaml_str);
  std::string line;
  std::string pending_key;  // key waiting for array/object children
  int pending_indent = -1;
  nlohmann::json* pending_parent = nullptr;

  while (std::getline(stream, line)) {
    // Skip completely empty lines
    if (line.find_first_not_of(" \t\r\n") == std::string::npos)
      continue;

    // Compute indentation (number of leading spaces)
    int indent = 0;
    for (char c : line) {
      if (c == ' ')
        ++indent;
      else if (c == '\t')
        indent += 2;
      else
        break;
    }

    std::string trimmed = line.substr(line.find_first_not_of(" \t"));
    // Remove trailing whitespace/CR
    while (!trimmed.empty() &&
           (trimmed.back() == ' ' || trimmed.back() == '\t' ||
            trimmed.back() == '\r')) {
      trimmed.pop_back();
    }

    if (trimmed.empty())
      continue;

    // Pop stack until we find a context at a lower indent level.
    // The '- ' array-item check was removed so that object-array items
    // (e.g. the commands: list) correctly close the previous item's
    // context before starting a new one.
    while (ctx.size() > 1 && ctx.top().first > indent) {
      ctx.pop();
    }

    // Handle pending key: if this line is indented deeper, create sub-object
    if (!pending_key.empty() && indent > pending_indent) {
      // Check if it's an array item or a sub-object key
      if (trimmed[0] == '-') {
        // Array: create array at pending key
        (*pending_parent)[pending_key] = nlohmann::json::array();
        // Don't push to stack — array items handled below
      } else {
        // Sub-object
        (*pending_parent)[pending_key] = nlohmann::json::object();
        ctx.push({indent, &(*pending_parent)[pending_key]});
      }
      pending_key.clear();
    } else if (!pending_key.empty()) {
      // Same or lower indent — pending key has empty value
      (*pending_parent)[pending_key] = "";
      pending_key.clear();
    }

    nlohmann::json* current = ctx.top().second;

    // Array item: "- value"
    if (trimmed[0] == '-') {
      std::string val = trimmed.substr(1);
      val.erase(0, val.find_first_not_of(" \t"));
      if (val.empty())
        continue;

      // Remove quotes
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val = val.substr(1, val.size() - 2);
      }

      // Find the array to append to — it's the last key added at parent level
      // that is an array. We look for the pending_key's array or the last
      // array. The array was already created when we saw the pending key +
      // deeper indent. Walk current object to find the last array value
      nlohmann::json* arr = nullptr;

      // Check pending parent first (the array might be one level up)
      for (auto it = current->begin(); it != current->end(); ++it) {
        if (it->is_array()) {
          arr = &(*it);
        }
      }

      if (arr) {
        // Detect "- key: value" — start of an object in the array.
        // YAML requires a space (or tab) after the colon in "key: v",
        // whereas URLs use "://" and times use "hh:mm" (digit after
        // colon).  We check both the key characters AND that the
        // character immediately following the colon is ' ', '\t', or
        // end-of-string.  This correctly rejects "https://example.com"
        // and "12:34" while accepting "name: value" and "name:".
        size_t colon_in_val = val.find(':');
        bool is_obj_item = false;
        if (colon_in_val != std::string::npos && colon_in_val > 0) {
          // Colon must be followed by space, tab, or end-of-string.
          char after =
              (colon_in_val + 1 < val.size()) ? val[colon_in_val + 1] : ' ';
          if (after == ' ' || after == '\t') {
            is_obj_item = true;
            for (size_t i = 0; i < colon_in_val; ++i) {
              char c = val[i];
              if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' &&
                  c != '-') {
                is_obj_item = false;
                break;
              }
            }
          }
        }

        if (is_obj_item) {
          // Push a new object into the array and enter its context.
          arr->push_back(nlohmann::json::object());
          nlohmann::json& obj_ref = arr->back();
          std::string obj_key = val.substr(0, colon_in_val);
          std::string obj_val = val.substr(colon_in_val + 1);
          obj_val.erase(0, obj_val.find_first_not_of(" \t"));
          while (!obj_val.empty() &&
                 (obj_val.back() == ' ' || obj_val.back() == '\t')) {
            obj_val.pop_back();
          }
          if (obj_val.size() >= 2 && obj_val.front() == '"' &&
              obj_val.back() == '"') {
            obj_val = obj_val.substr(1, obj_val.size() - 2);
          }
          if (!obj_val.empty())
            obj_ref[obj_key] = obj_val;
          // indent+1: any sub-property at deeper indent goes here;
          // the next '-' at 'indent' will pop this entry cleanly.
          ctx.push({indent + 1, &obj_ref});
        } else {
          arr->push_back(val);
        }
      }
      continue;
    }

    // Key-value or key-only
    size_t colon_pos = trimmed.find(':');
    if (colon_pos == std::string::npos)
      continue;

    std::string key = trimmed.substr(0, colon_pos);
    std::string value = trimmed.substr(colon_pos + 1);

    // Trim key and value
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
      key.pop_back();
    value.erase(0, value.find_first_not_of(" \t"));
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
      value.pop_back();

    if (value.empty()) {
      // Key with no value — could be sub-object or array on next lines
      pending_key = key;
      pending_indent = indent;
      pending_parent = current;
      continue;
    }

    // Inline array: [item1, item2]
    if (value.front() == '[' && value.back() == ']') {
      std::string inner = value.substr(1, value.size() - 2);
      nlohmann::json arr = nlohmann::json::array();
      std::istringstream items(inner);
      std::string item;
      while (std::getline(items, item, ',')) {
        item.erase(0, item.find_first_not_of(" \t\""));
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t' ||
                                 item.back() == '"')) {
          item.pop_back();
        }
        if (!item.empty())
          arr.push_back(item);
      }
      (*current)[key] = arr;
    } else if (value == "true") {
      (*current)[key] = true;
    } else if (value == "false") {
      (*current)[key] = false;
    } else {
      // String value — remove quotes if present
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
      }
      (*current)[key] = value;
    }
  }

  // Flush final pending key
  if (!pending_key.empty() && pending_parent) {
    (*pending_parent)[pending_key] = "";
  }

  return root;
}

std::vector<SkillMetadata>
SkillLoader::LoadSkills(const SkillsConfig& skills_config,
                        const std::filesystem::path& workspace_path) {
  // Build ordered directory list: workspace > user > extraDirs
  std::vector<std::filesystem::path> dirs;
  dirs.push_back(workspace_path / "skills");

  std::string home_str;
  const char* home = std::getenv("HOME");
  if (home)
    home_str = home;
  dirs.push_back(std::filesystem::path(home_str.empty() ? "/tmp" : home_str) /
                 ".quantclaw" / "skills");

  for (const auto& extra : skills_config.load.extra_dirs) {
    dirs.push_back(std::filesystem::path(extra));
  }

  // Load from each directory, dedup by name (first wins)
  std::unordered_set<std::string> seen_names;
  std::vector<SkillMetadata> result;

  for (const auto& dir : dirs) {
    auto skills = LoadSkillsFromDirectory(dir);
    for (auto& skill : skills) {
      if (seen_names.count(skill.name))
        continue;

      // Check per-skill disable
      auto it = skills_config.entries.find(skill.name);
      if (it != skills_config.entries.end() && !it->second.enabled) {
        logger_->debug("Skill '{}' disabled via config", skill.name);
        continue;
      }

      seen_names.insert(skill.name);
      result.push_back(std::move(skill));
    }
  }

  logger_->info("Loaded {} skills from {} directories", result.size(),
                dirs.size());
  return result;
}

}  // namespace quantclaw
