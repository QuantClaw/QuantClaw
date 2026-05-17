// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <regex>
#include <stack>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace quantclaw {

// Install method for skill auto-install.
// Supports both QuantClaw flat format (method/formula/binary) and
// OpenClaw array format (kind/formula/package/bins).
struct SkillInstallInfo {
  std::string method;   // "node", "go", "uv", "download", "apt", "brew"
  std::string formula;  // package/URL to install
  std::string binary;   // expected binary after install

  // OpenClaw extended fields
  std::string kind;               // alias for method
  std::string id;                 // optional install spec identifier
  std::string label;              // human-readable label
  std::string package;            // npm/go package name
  std::string module;             // node module name
  std::string url;                // download URL
  std::string archive;            // archive type
  std::string target_dir;         // target directory for download
  std::vector<std::string> bins;  // expected binaries (OpenClaw format)
  std::vector<std::string> os;    // OS restriction for this install
  bool extract = false;           // extract archive
  int strip_components = 0;       // tar strip-components

  // Returns the effective install method (kind takes precedence if set)
  std::string EffectiveMethod() const {
    return kind.empty() ? method : kind;
  }

  // Returns the effective package identifier
  std::string EffectiveFormula() const {
    if (!formula.empty())
      return formula;
    if (!package.empty())
      return package;
    if (!module.empty())
      return module;
    if (!url.empty())
      return url;
    return "";
  }

  // Returns the first expected binary
  std::string EffectiveBinary() const {
    if (!binary.empty())
      return binary;
    if (!bins.empty())
      return bins.front();
    return "";
  }
};

// Slash command defined in a skill
struct SkillCommand {
  std::string name;  // command name (no leading /)
  std::string description;
  std::string tool_name;  // tool to invoke
  std::string arg_mode;   // "freeform", "json", "none"
};

// ============================================================================
// Skill data structures follow the SKILL.md file structure:
//
//   SKILL.md file
//   ├── YAML Frontmatter → SkillMetadata = SkillMiniMetadata + SkillExtraMetadata
//   └── Markdown Body    → SkillContent
//
//   SkillFullData = SkillMiniMetadata + SkillExtraMetadata + SkillContent
//                  = SkillMetadata + SkillContent
//
// - SkillMiniMetadata:  lightweight fields for pre-loading, discovery,
//   and progressive disclosure. Populated even when load_content=false.
// - SkillExtraMetadata:  extended fields for full activation — gating
//   requirements, install instructions, and execution context modifications.
// - SkillMetadata:       complete YAML frontmatter = mini + extra.
// - SkillContent:        non-YAML-frontmatter content (markdown body).
// - SkillFullData:       complete SKILL.md file = mini + extra + body.
// ============================================================================

// ---------------------------------------------------------------------------
// Lightweight metadata for pre-loading, discovery, and progressive disclosure.
// Parsed from SKILL.md YAML frontmatter; sufficient for SkillLoaderMeta and
// SkillMetaTool::BuildSchema without reading the full markdown content.
// Skill mini meta data references:https://code.claude.com/docs/zh-CN/skills
// ---------------------------------------------------------------------------
struct SkillMiniMetadata {
  // Identity
  std::string name;
  std::string description;
  std::string emoji;  // display emoji (e.g. "📄")

  // Gating flags (lightweight)
  bool always = false;  // skip all gating
  std::vector<std::string> os_restrict;  // e.g. ["linux", "darwin", "win32"]

  // Invocation control
  bool disable_model_invocation = false;  // LLM cannot invoke this skill
  bool user_invocable = true;             // user can invoke via /skill

  // Resource directories (derived from file path, not YAML frontmatter)
  std::string root_dir;
  std::string scripts_dir;
  std::string references_dir;
  std::string assets_dir;
};

// ---------------------------------------------------------------------------
// Extended metadata loaded when the skill is fully activated.
// Contains gating requirements, install instructions, slash commands,
// and execution context modifications (Claude Skills dual-context injection).
// ---------------------------------------------------------------------------
struct SkillExtraMetadata {
  // Gating requirements
  std::vector<std::string> required_bins;  // all must exist
  std::vector<std::string> required_envs;  // all must be set
  std::vector<std::string> any_bins;       // at least one must exist
  std::vector<std::string> config_files;   // required config files
  std::string primary_env;                 // primary environment variable

  // References
  std::string homepage;   // skill homepage URL
  std::string skill_key;  // alternative skill key

  // Install & commands
  std::vector<SkillInstallInfo> installs;  // auto-install instructions
  std::vector<SkillCommand> commands;      // slash commands

  // Execution context modification (Claude Skills dual-context injection)
  std::vector<std::string> allowed_tools;  // e.g. ["Bash(git:*)", "read"]
  std::string model_override;              // force model switch e.g. "opus"
};

// ---------------------------------------------------------------------------
// Complete YAML frontmatter = lightweight + extended.
// Describes all fields parsed from the SKILL.md YAML frontmatter;
// does NOT include the non-YAML-frontmatter content (markdown body).
// ---------------------------------------------------------------------------
struct SkillMetadata {
  SkillMiniMetadata mini;
  SkillExtraMetadata extra;
};

// ---------------------------------------------------------------------------
// Non-YAML-frontmatter content of a SKILL.md file.
// The markdown body that follows the closing --- marker.
// ---------------------------------------------------------------------------
struct SkillContent {
  std::string content;  // markdown body after YAML frontmatter
};

// ---------------------------------------------------------------------------
// Complete SKILL.md file data = mini + extra + body.
// This is the primary data structure returned by ParseSkillFile and used
// throughout the skill pipeline (loader, meta-tool, agent loop).
// ---------------------------------------------------------------------------
struct SkillFullData {
  SkillMiniMetadata mini;
  SkillExtraMetadata extra;
  SkillContent body;  // non-YAML-frontmatter content
};

// Skill 文件解析器
// 注意：此类不是线程安全的，不可并发解析。
// 同一实例可顺序多次调用 ParseSkillFile，每次解析前会自动 Reset。
class SkillParser {
 public:
  // 重置解析状态，允许同一实例复用
  void Reset();

  // 解析 SKILL.md 文件
  SkillFullData ParseSkillFile(const std::string& skill_file,
                               bool load_content = true);

  // 解析 YAML frontmatter 字符串
  nlohmann::json ParseYamlFrontmatter(const std::string& yaml_str);

 private:
  // 解析状态
  SkillFullData skill_full_data_;
  size_t frontmatter_len_ = 0;
  
  // YAML 解析状态
  struct YamlParserContext {
    nlohmann::json root = nlohmann::json::object();
    std::stack<std::pair<int, nlohmann::json*>> ctx_stack;
    std::string pending_key;
    int pending_indent = -1;
    nlohmann::json* pending_parent = nullptr;
    std::string multiline_key;
    int multiline_indent = -1;
    std::string multiline_value;
    bool multiline_fold = false;
    bool in_multiline = false;
  };
  YamlParserContext yaml_ctx_;

  // YAML 行解析结果
  struct ParsedLine {
    int indent = 0;
    std::string trimmed;
    bool is_empty = false;
  };

  // YAML 解析方法
  ParsedLine ParseYamlLine(const std::string& line);
  std::string CollapseYamlSpaces(const std::string& text);
  void FinalizeYamlMultilineString();
  bool HandleYamlMultilineContinuation(const ParsedLine& parsed);
  void UpdateYamlContextStack(int indent);
  void HandleYamlPendingKey(const ParsedLine& parsed);
  void FinalizeYamlPendingKey();
  void ParseYamlArrayItem(const ParsedLine& parsed);
  bool IsYamlObjectArrayItem(const std::string& val);
  void ParseYamlKeyValue(const ParsedLine& parsed, nlohmann::json* current);
  bool IsYamlMultilineIndicator(const std::string& value);
  nlohmann::json ParseYamlValue(const std::string& value);
  void ResetYamlContext();

  // 文件读取
  std::string ReadFileContent(const std::string& skill_file);
  bool ExtractFrontmatter(const std::string& content, std::string& frontmatter);

  // 元数据解析
  void ParseMiniMetadata(const nlohmann::json& metadata);
  void ParseExtraMetadata(const nlohmann::json& metadata);
  void ParseRequires(const nlohmann::json& metadata);
  void ParseOpenclawFields(const nlohmann::json& metadata);
  void ParseExecutionContext(const nlohmann::json& metadata);
  void ParseInstallInfo(const nlohmann::json& metadata);
  void ParseCommands(const nlohmann::json& metadata);

  // 辅助函数
  std::vector<std::string> ExtractStringArray(const nlohmann::json& arr);
  void ExtractRequires(const nlohmann::json& reqs);
  std::string CheckResourceDir(const std::string& root_dir, const std::string& subdir);
  void SetDefaultName(const std::string& skill_file);
  void SetResourceDirs(const std::string& skill_file);
  void ExtractContent(const std::string& content, bool load_content);
};

}  // namespace quantclaw
