// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/stream_normalization.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "quantclaw/common/string_util.hpp"
#include "quantclaw/core/content_block.hpp"

namespace quantclaw {
namespace {

constexpr size_t kReplayToolCallNameMaxChars = 64;

bool HasNonWhitespace(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
    return !std::isspace(ch);
  });
}

std::string NormalizeToolNameToken(std::string_view raw) {
  std::string normalized;
  normalized.reserve(raw.size());
  bool last_was_separator = false;
  for (unsigned char ch : raw) {
    if (std::isalnum(ch)) {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
      last_was_separator = false;
      continue;
    }
    if (ch == '.' || ch == '_' || ch == '-' || ch == '/') {
      if (!last_was_separator && !normalized.empty()) {
        normalized.push_back('.');
      }
      last_was_separator = true;
    }
  }
  while (!normalized.empty() && normalized.back() == '.') {
    normalized.pop_back();
  }
  return normalized;
}

const std::unordered_map<std::string, std::string>& HtmlEntityMap() {
  static const auto* entities =
      new std::unordered_map<std::string, std::string>{{"amp", "&"},
                                                       {"quot", "\""},
                                                       {"apos", "'"},
                                                       {"#39", "'"},
                                                       {"lt", "<"},
                                                       {"gt", ">"}};
  return *entities;
}

std::optional<std::string> DecodeHtmlEntity(std::string_view entity) {
  const auto& entities = HtmlEntityMap();
  auto it = entities.find(std::string(entity));
  if (it != entities.end()) {
    return it->second;
  }

  if (entity.size() >= 2 && entity[0] == '#') {
    int codepoint = -1;
    try {
      if (entity[1] == 'x' || entity[1] == 'X') {
        codepoint = std::stoi(std::string(entity.substr(2)), nullptr, 16);
      } else {
        codepoint = std::stoi(std::string(entity.substr(1)), nullptr, 10);
      }
    } catch (...) {
      return std::nullopt;
    }
    if (codepoint < 0 || codepoint > 0x10FFFF) {
      return std::nullopt;
    }
    std::string decoded;
    if (codepoint <= 0x7F) {
      decoded.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      decoded.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
      decoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
      decoded.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
      decoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      decoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      decoded.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
      decoded.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
      decoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      decoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return decoded;
  }

  return std::nullopt;
}

std::string DecodeHtmlEntities(std::string_view raw) {
  std::string decoded;
  decoded.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] != '&') {
      decoded.push_back(raw[i]);
      continue;
    }
    const auto semi = raw.find(';', i + 1);
    if (semi == std::string_view::npos) {
      decoded.push_back(raw[i]);
      continue;
    }
    const auto entity = raw.substr(i + 1, semi - i - 1);
    auto replacement = DecodeHtmlEntity(entity);
    if (!replacement) {
      decoded.push_back(raw[i]);
      continue;
    }
    decoded += *replacement;
    i = semi;
  }
  return decoded;
}

void DecodeHtmlEntitiesInJson(nlohmann::json* value) {
  if (!value) {
    return;
  }
  if (value->is_string()) {
    auto& str = value->get_ref<std::string&>();
    str = DecodeHtmlEntities(str);
    return;
  }
  if (value->is_array()) {
    for (auto& item : *value) {
      DecodeHtmlEntitiesInJson(&item);
    }
    return;
  }
  if (value->is_object()) {
    for (auto& [_, item] : value->items()) {
      DecodeHtmlEntitiesInJson(&item);
    }
  }
}

std::string MakeFallbackToolCallId(size_t ordinal) {
  return "call_auto_" + std::to_string(ordinal);
}

std::string EnsureUniqueToolCallId(const std::string& preferred,
                                   std::unordered_set<std::string>* seen_ids) {
  if (!seen_ids) {
    return preferred;
  }
  std::string candidate = preferred;
  size_t ordinal = seen_ids->size() + 1;
  if (!HasNonWhitespace(candidate)) {
    candidate = MakeFallbackToolCallId(ordinal);
  }
  if (seen_ids->insert(candidate).second) {
    return candidate;
  }
  do {
    candidate = MakeFallbackToolCallId(ordinal++);
  } while (!seen_ids->insert(candidate).second);
  return candidate;
}

std::string ResolveCaseInsensitiveAllowedToolName(
    std::string_view raw_name,
    const std::unordered_map<std::string, std::string>& folded_allowed) {
  auto it = folded_allowed.find(ToLower(raw_name));
  return it != folded_allowed.end() ? it->second : std::string{};
}

std::vector<std::string> BuildStructuredToolNameCandidates(
    std::string_view raw_name) {
  const std::string trimmed = Trim(raw_name);
  if (trimmed.empty()) {
    return {};
  }

  std::vector<std::string> candidates;
  std::unordered_set<std::string> seen;
  auto add = [&](std::string candidate) {
    candidate = Trim(candidate);
    if (candidate.empty() || seen.count(candidate) > 0) {
      return;
    }
    seen.insert(candidate);
    candidates.push_back(std::move(candidate));
  };

  add(trimmed);
  add(NormalizeToolNameToken(trimmed));

  std::string normalized_delimiter = trimmed;
  std::replace(normalized_delimiter.begin(), normalized_delimiter.end(), '/',
               '.');
  add(normalized_delimiter);
  add(NormalizeToolNameToken(normalized_delimiter));

  auto segments = Split(normalized_delimiter, '.');
  std::vector<std::string> filtered;
  for (auto& segment : segments) {
    const auto part = Trim(segment);
    if (!part.empty()) {
      filtered.push_back(part);
    }
  }
  if (filtered.size() > 1) {
    for (size_t index = 1; index < filtered.size(); ++index) {
      std::string suffix;
      for (size_t i = index; i < filtered.size(); ++i) {
        if (!suffix.empty()) {
          suffix += '.';
        }
        suffix += filtered[i];
      }
      add(suffix);
      add(NormalizeToolNameToken(suffix));
    }
  }

  return candidates;
}

std::string ResolveStructuredAllowedToolName(
    std::string_view raw_name,
    const std::vector<std::string>& allowed_tool_names,
    const std::unordered_map<std::string, std::string>& normalized_allowed,
    const std::unordered_map<std::string, std::string>& folded_allowed) {
  if (allowed_tool_names.empty()) {
    return {};
  }

  for (const auto& candidate : BuildStructuredToolNameCandidates(raw_name)) {
    if (std::find(allowed_tool_names.begin(), allowed_tool_names.end(),
                  candidate) != allowed_tool_names.end()) {
      return candidate;
    }
  }
  for (const auto& candidate : BuildStructuredToolNameCandidates(raw_name)) {
    auto it = normalized_allowed.find(candidate);
    if (it != normalized_allowed.end()) {
      return it->second;
    }
  }
  for (const auto& candidate : BuildStructuredToolNameCandidates(raw_name)) {
    auto resolved = ResolveCaseInsensitiveAllowedToolName(candidate, folded_allowed);
    if (!resolved.empty()) {
      return resolved;
    }
  }
  return {};
}

std::string InferToolNameFromToolCallId(
    std::string_view raw_id,
    const std::vector<std::string>& allowed_tool_names,
    const std::unordered_map<std::string, std::string>& normalized_allowed,
    const std::unordered_map<std::string, std::string>& folded_allowed) {
  const std::string id = Trim(raw_id);
  if (id.empty() || allowed_tool_names.empty()) {
    return {};
  }

  const std::regex trailing_index_re("[:._/-]\\d+$");
  const std::regex trailing_digits_re("\\d+$");
  const std::regex function_prefix_re("^functions?[._-]?", std::regex::icase);
  const std::regex tool_prefix_re("^tools?[._-]?", std::regex::icase);

  std::unordered_set<std::string> candidate_tokens;
  auto add_token = [&](std::string token) {
    token = Trim(token);
    if (token.empty()) {
      return;
    }
    candidate_tokens.insert(token);
    candidate_tokens.insert(std::regex_replace(token, trailing_index_re, ""));
    candidate_tokens.insert(std::regex_replace(token, trailing_digits_re, ""));

    std::string normalized_delimiter = token;
    std::replace(normalized_delimiter.begin(), normalized_delimiter.end(), '/',
                 '.');
    candidate_tokens.insert(normalized_delimiter);
    candidate_tokens.insert(
        std::regex_replace(normalized_delimiter, trailing_index_re, ""));
    candidate_tokens.insert(
        std::regex_replace(normalized_delimiter, trailing_digits_re, ""));

    for (const auto& prefix_pattern : {function_prefix_re, tool_prefix_re}) {
      auto stripped =
          std::regex_replace(normalized_delimiter, prefix_pattern, "");
      if (stripped != normalized_delimiter) {
        candidate_tokens.insert(stripped);
        candidate_tokens.insert(
            std::regex_replace(stripped, trailing_index_re, ""));
        candidate_tokens.insert(
            std::regex_replace(stripped, trailing_digits_re, ""));
      }
    }
  };

  const auto colon = id.find(':');
  add_token(id);
  add_token(colon == std::string::npos ? id : id.substr(0, colon));

  std::string single_match;
  for (const auto& token : candidate_tokens) {
    auto matched = ResolveStructuredAllowedToolName(token, allowed_tool_names,
                                                    normalized_allowed,
                                                    folded_allowed);
    if (matched.empty()) {
      continue;
    }
    if (!single_match.empty() && single_match != matched) {
      return {};
    }
    single_match = matched;
  }
  return single_match;
}

bool LooksLikeMalformedToolNameCounter(std::string_view raw_name) {
  std::string normalized = Trim(raw_name);
  std::replace(normalized.begin(), normalized.end(), '/', '.');
  return std::regex_search(normalized,
                           std::regex("^(?:functions?|tools?)[._-]?",
                                      std::regex::icase)) &&
         std::regex_search(normalized,
                           std::regex("(?:[:._-]\\d+|\\d+)$"));
}

std::string ResolveAllowedToolName(
    std::string_view raw_name, const StreamNormalizationContext& context,
    std::string_view raw_tool_call_id = {}) {
  const std::string trimmed = Trim(raw_name);
  if (trimmed.empty()) {
    return InferToolNameFromToolCallId(raw_tool_call_id, context.allowed_tool_names,
                                       context.normalized_allowed_tool_names,
                                       context.folded_allowed_tool_names);
  }
  if (context.allowed_tool_names.empty()) {
    return trimmed;
  }

  if (std::find(context.allowed_tool_names.begin(),
                context.allowed_tool_names.end(),
                trimmed) != context.allowed_tool_names.end()) {
    return trimmed;
  }

  auto normalized = NormalizeToolNameToken(trimmed);
  auto normalized_it = context.normalized_allowed_tool_names.find(normalized);
  if (normalized_it != context.normalized_allowed_tool_names.end()) {
    return normalized_it->second;
  }

  auto case_insensitive =
      ResolveCaseInsensitiveAllowedToolName(trimmed,
                                            context.folded_allowed_tool_names);
  if (!case_insensitive.empty()) {
    return case_insensitive;
  }
  case_insensitive =
      ResolveCaseInsensitiveAllowedToolName(normalized,
                                            context.folded_allowed_tool_names);
  if (!case_insensitive.empty()) {
    return case_insensitive;
  }

  auto inferred_from_name = InferToolNameFromToolCallId(
      trimmed, context.allowed_tool_names, context.normalized_allowed_tool_names,
      context.folded_allowed_tool_names);
  if (!inferred_from_name.empty()) {
    return inferred_from_name;
  }

  if (LooksLikeMalformedToolNameCounter(trimmed)) {
    return trimmed;
  }

  auto structured = ResolveStructuredAllowedToolName(
      trimmed, context.allowed_tool_names, context.normalized_allowed_tool_names,
      context.folded_allowed_tool_names);
  return structured.empty() ? trimmed : structured;
}

nlohmann::json ParseArgumentsWithRepair(std::string_view raw_arguments,
                                       bool decode_html_entities) {
  std::string candidate = Trim(raw_arguments);
  if (decode_html_entities) {
    candidate = DecodeHtmlEntities(candidate);
  }
  if (candidate.empty()) {
    return nlohmann::json::object();
  }

  auto parsed = nlohmann::json::parse(candidate, nullptr, false);
  if (!parsed.is_discarded()) {
    if (decode_html_entities) {
      DecodeHtmlEntitiesInJson(&parsed);
    }
    return parsed;
  }

  const auto start = candidate.find_first_of("{[");
  if (start != std::string::npos) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = start; i < candidate.size(); ++i) {
      const char ch = candidate[i];
      if (in_string) {
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '"') {
          in_string = false;
        }
        continue;
      }
      if (ch == '"') {
        in_string = true;
        continue;
      }
      if (ch == '{' || ch == '[') {
        ++depth;
      } else if (ch == '}' || ch == ']') {
        --depth;
        if (depth == 0) {
          auto repaired = candidate.substr(start, i - start + 1);
          auto repaired_json = nlohmann::json::parse(repaired, nullptr, false);
          if (!repaired_json.is_discarded()) {
            if (decode_html_entities) {
              DecodeHtmlEntitiesInJson(&repaired_json);
            }
            return repaired_json;
          }
          break;
        }
      }
    }
  }

  return nlohmann::json();
}

bool NormalizeToolCall(ToolCall* tool_call,
                       const StreamNormalizationContext& context,
                       std::unordered_set<std::string>* seen_ids,
                       size_t ordinal) {
  if (!tool_call) {
    return false;
  }

  tool_call->name =
      ResolveAllowedToolName(tool_call->name, context, tool_call->id);
  if (!HasNonWhitespace(tool_call->name)) {
    return false;
  }
  if (context.decode_html_entities) {
    DecodeHtmlEntitiesInJson(&tool_call->arguments);
  }
  if (tool_call->arguments.is_null()) {
    tool_call->arguments = nlohmann::json::object();
  }
  if (!tool_call->arguments.is_object() && !tool_call->arguments.is_array()) {
    if (context.logger) {
      context.logger->warn(
          "Dropping malformed tool call '{}' because arguments are not structured",
          tool_call->name);
    }
    return false;
  }
  tool_call->id = EnsureUniqueToolCallId(Trim(tool_call->id), seen_ids);
  if (tool_call->id.empty()) {
    tool_call->id =
        EnsureUniqueToolCallId(MakeFallbackToolCallId(ordinal), seen_ids);
  }
  return true;
}

void NormalizeToolUseBlock(ContentBlock* block,
                           const StreamNormalizationContext& context,
                           std::unordered_set<std::string>* seen_ids,
                           size_t ordinal) {
  if (!block || block->type != "tool_use") {
    return;
  }
  block->name = ResolveAllowedToolName(block->name, context, block->id);
  if (context.decode_html_entities) {
    DecodeHtmlEntitiesInJson(&block->input);
  }
  if (block->input.is_null()) {
    block->input = nlohmann::json::object();
  }
  block->id = EnsureUniqueToolCallId(Trim(block->id), seen_ids);
  if (block->id.empty()) {
    block->id = EnsureUniqueToolCallId(MakeFallbackToolCallId(ordinal), seen_ids);
  }
}

}  // namespace

std::vector<std::string> ExtractAllowedToolNames(
    const ChatCompletionRequest& request) {
  std::vector<std::string> names;
  names.reserve(request.tools.size());
  for (const auto& tool : request.tools) {
    if (!tool.is_object()) {
      continue;
    }
    if (tool.value("type", "") != "function") {
      continue;
    }
    if (!tool.contains("function") || !tool["function"].is_object()) {
      continue;
    }
    const auto name = Trim(tool["function"].value("name", std::string{}));
    if (!name.empty()) {
      names.push_back(name);
    }
  }
  return names;
}

StreamNormalizationContext BuildStreamNormalizationContext(
    const std::string& provider_id, const std::string& api,
    const ChatCompletionRequest& request,
    const std::shared_ptr<spdlog::logger>& logger) {
  StreamNormalizationContext context;
  context.provider_id = provider_id;
  context.api = api;
  context.allowed_tool_names = ExtractAllowedToolNames(request);
  context.logger = logger;

  const auto folded_provider = ToLower(provider_id);
  const auto folded_api = ToLower(api);
  context.decode_html_entities = folded_provider == "xai" ||
                                 folded_provider == "grok" ||
                                 folded_api.find("html-entities") !=
                                     std::string::npos;

  for (const auto& name : context.allowed_tool_names) {
    context.normalized_allowed_tool_names.emplace(NormalizeToolNameToken(name),
                                                  name);
    context.folded_allowed_tool_names.emplace(ToLower(name), name);
  }
  return context;
}

void SanitizeReplayMessages(std::vector<Message>* messages,
                            const StreamNormalizationContext& context) {
  if (!messages) {
    return;
  }
  for (auto& message : *messages) {
    if (message.role != "assistant") {
      continue;
    }
    std::unordered_set<std::string> seen_ids;
    std::vector<ContentBlock> sanitized;
    size_t ordinal = 1;
    for (auto& block : message.content) {
      if (block.type != "tool_use") {
        sanitized.push_back(std::move(block));
        continue;
      }
      NormalizeToolUseBlock(&block, context, &seen_ids, ordinal++);
      if (!HasNonWhitespace(block.name) ||
          block.name.size() > kReplayToolCallNameMaxChars ||
          block.name.find_first_of(" \t\r\n") != std::string::npos) {
        if (context.logger) {
          context.logger->warn("Dropping malformed replay tool call with id='{}'",
                               block.id);
        }
        continue;
      }
      if (block.input.is_null()) {
        continue;
      }
      sanitized.push_back(std::move(block));
    }
    message.content = std::move(sanitized);
  }
}

void NormalizeToolCalls(std::vector<ToolCall>* tool_calls,
                        const StreamNormalizationContext& context,
                        std::unordered_set<std::string>* seen_ids) {
  if (!tool_calls) {
    return;
  }
  std::vector<ToolCall> normalized;
  normalized.reserve(tool_calls->size());
  size_t ordinal = 1;
  for (auto& tool_call : *tool_calls) {
    if (NormalizeToolCall(&tool_call, context, seen_ids, ordinal++)) {
      normalized.push_back(std::move(tool_call));
    }
  }
  *tool_calls = std::move(normalized);
}

std::vector<ToolCall> FinalizePendingToolCalls(
    const std::vector<PendingToolCallFragment>& pending,
    const StreamNormalizationContext& context,
    std::unordered_set<std::string>* seen_ids,
    std::vector<PendingToolCallFragment>* remaining) {
  std::vector<ToolCall> normalized;
  std::vector<PendingToolCallFragment> unresolved;
  normalized.reserve(pending.size());

  size_t ordinal = 1;
  for (const auto& fragment : pending) {
    ToolCall tool_call;
    tool_call.id = fragment.id;
    tool_call.name = fragment.name;
    tool_call.arguments =
        ParseArgumentsWithRepair(fragment.arguments, context.decode_html_entities);

    if (HasNonWhitespace(fragment.arguments) && tool_call.arguments.is_null()) {
      unresolved.push_back(fragment);
      continue;
    }
    if (NormalizeToolCall(&tool_call, context, seen_ids, ordinal++)) {
      normalized.push_back(std::move(tool_call));
    } else if (remaining) {
      unresolved.push_back(fragment);
    }
  }

  if (remaining) {
    *remaining = std::move(unresolved);
  }
  return normalized;
}

}  // namespace quantclaw
