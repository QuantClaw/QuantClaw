// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ─── String utilities ────────────────────────────────────────────────────────
//
// All functions that accept std::string_view work with std::string,
// const char*, and string literals without copies at the call site.

namespace quantclaw {

// ── Whitespace trimming ──────────────────────────────────────────────────────

inline std::string TrimLeft(std::string_view s) {
  auto it = std::find_if(s.begin(), s.end(),
                         [](unsigned char c) { return !std::isspace(c); });
  return std::string(it, s.end());
}

inline std::string TrimRight(std::string_view s) {
  auto it = std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char c) { return !std::isspace(c); });
  return std::string(s.begin(), it.base());
}

inline std::string Trim(std::string_view s) {
  return TrimLeft(TrimRight(s));
}

// ── Case conversion
// ───────────────────────────────────────────────────────────

inline std::string ToLower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

inline std::string ToUpper(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return out;
}

// Case-insensitive equality.
inline bool Iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  return std::equal(a.begin(), a.end(), b.begin(),
                    [](unsigned char x, unsigned char y) {
                      return std::tolower(x) == std::tolower(y);
                    });
}

// ── Prefix / suffix checks ───────────────────────────────────────────────────

inline bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

inline bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.substr(s.size() - suffix.size()) == suffix;
}

// ── Split ────────────────────────────────────────────────────────────────────
//
// Splits on a single character delimiter. Empty tokens are preserved:
//   Split("a,,b", ',') → {"a", "", "b"}

inline std::vector<std::string> Split(std::string_view s, char delim) {
  std::vector<std::string> result;
  std::string_view::size_type start = 0;
  for (std::string_view::size_type i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == delim) {
      result.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return result;
}

// ── Join ─────────────────────────────────────────────────────────────────────
//
// Join({"a","b","c"}, ", ")          → "a, b, c"
// JoinWith(nums, ", ", to_string)    → "1, 2, 3"

template <typename Container>
std::string Join(const Container& c, std::string_view sep) {
  std::string out;
  bool first = true;
  for (const auto& v : c) {
    if (!first)
      out += sep;
    out += v;
    first = false;
  }
  return out;
}

template <typename Container, typename F>
std::string JoinWith(const Container& c, std::string_view sep, F&& transform) {
  std::string out;
  bool first = true;
  for (const auto& v : c) {
    if (!first)
      out += sep;
    out += std::forward<F>(transform)(v);
    first = false;
  }
  return out;
}

}  // namespace quantclaw
