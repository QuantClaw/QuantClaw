// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

// ─── Safe numeric parsing ────────────────────────────────────────────────────
//
// All functions return std::nullopt on:
//   • non-numeric input
//   • trailing non-numeric characters
//   • out-of-[min, max] range
//   • empty input
//
// Uses std::from_chars (C++17): no exceptions, no locale, no heap.

namespace quantclaw {

// ParseInt<T>(str, min, max)
// T must be an integer type (checked at compile time).
template <typename T = int,
          typename = std::enable_if_t<std::is_integral_v<T>>>
std::optional<T> ParseInt(
    std::string_view s,
    T min_val = std::numeric_limits<T>::min(),
    T max_val = std::numeric_limits<T>::max()) {

    if (s.empty()) return std::nullopt;

    T value{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);

    // Must consume the entire string and succeed.
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;

    if (value < min_val || value > max_val) return std::nullopt;

    return value;
}

// ── Named convenience wrappers ───────────────────────────────────────────────

// Valid TCP/IP port: 1–65535.
inline std::optional<uint16_t> ParsePort(std::string_view s) {
    return ParseInt<uint16_t>(s, 1, 65535);
}

// Positive integer (≥ 1), commonly used for counts / limits.
inline std::optional<int> ParsePositiveInt(std::string_view s) {
    return ParseInt<int>(s, 1, std::numeric_limits<int>::max());
}

// Non-negative integer (≥ 0).
inline std::optional<int> ParseNonNegativeInt(std::string_view s) {
    return ParseInt<int>(s, 0, std::numeric_limits<int>::max());
}

// Duration in milliseconds with an upper bound of 24 h.
inline std::optional<int> ParseMilliseconds(std::string_view s) {
    return ParseInt<int>(s, 0, 86'400'000);
}

}  // namespace quantclaw
