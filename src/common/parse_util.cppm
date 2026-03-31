// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.common.parse_util;

import std;

export namespace quantclaw {

// ParseInt<T>(str, min, max)
// T must be an integer type (checked at compile time).
template <typename T = int, typename = std::enable_if_t<std::is_integral_v<T>>>
std::optional<T> ParseInt(std::string_view s,
                          T min_val = std::numeric_limits<T>::min(),
                          T max_val = std::numeric_limits<T>::max()) {
  if (s.empty())
    return std::nullopt;

  T value{};
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);

  // Must consume the entire string and succeed.
  if (ec != std::errc{} || ptr != s.data() + s.size())
    return std::nullopt;

  if (value < min_val || value > max_val)
    return std::nullopt;

  return value;
}

// Valid TCP/IP port: 1–65535.
inline std::optional<std::uint16_t> ParsePort(std::string_view s) {
  return ParseInt<std::uint16_t>(s, 1, 65535);
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