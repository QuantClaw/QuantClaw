// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <nlohmann/json.hpp>

export module nlohmann.json;

export namespace nlohmann {

using json = ::nlohmann::json;
using ordered_json = ::nlohmann::ordered_json;

}  // namespace nlohmann
