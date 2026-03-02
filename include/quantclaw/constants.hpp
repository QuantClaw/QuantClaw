// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ============================================================
// QuantClaw project-wide named constants
//
// All "magic numbers" that appear across multiple files must be
// defined here so they can be changed in one place.
// ============================================================

namespace quantclaw {

// ------------------------------------------------------------
// Network ports
// ------------------------------------------------------------

/// WebSocket RPC gateway port (primary IPC between CLI and daemon)
inline constexpr int kDefaultGatewayPort = 18800;

/// HTTP/REST + Dashboard port served by the control-UI
inline constexpr int kDefaultHttpPort = 18801;

/// Sidecar IPC port range (reserved for adapter subprocesses)
inline constexpr int kSidecarPortRangeStart = 18802;
inline constexpr int kSidecarPortRangeEnd   = 18899;

/// Legacy default port used before the 18800 standardisation.
/// Kept only for backwards-compatible daemon/service parameter defaults.
inline constexpr int kLegacyGatewayPort = 18789;

// ------------------------------------------------------------
// Network addresses
// ------------------------------------------------------------

/// Default loopback bind address
inline constexpr const char* kDefaultBindAddress = "127.0.0.1";

/// Default WebSocket gateway URL (used by CLI commands when no config exists)
inline constexpr const char* kDefaultGatewayUrl = "ws://127.0.0.1:18800";

// ------------------------------------------------------------
// Agent / LLM defaults
// ------------------------------------------------------------

/// Hard limit on the number of agent reasoning iterations per request
inline constexpr int kDefaultMaxIterations = 15;

/// Default LLM temperature (0 = deterministic, 1 = very creative)
inline constexpr double kDefaultTemperature = 0.7;

/// Maximum output tokens to request from the LLM
inline constexpr int kDefaultMaxTokens = 4096;

// ------------------------------------------------------------
// Session compaction defaults
// ------------------------------------------------------------

/// Trigger compaction when message history exceeds this count
inline constexpr int kDefaultCompactMaxMessages = 100;

/// Number of recent messages to retain after compaction
inline constexpr int kDefaultCompactKeepRecent = 20;

/// Trigger compaction when estimated token count exceeds this value
inline constexpr int kDefaultCompactMaxTokens = 100000;

// ------------------------------------------------------------
// Timeout defaults (seconds)
// ------------------------------------------------------------

/// Default per-request timeout for LLM provider HTTP calls
inline constexpr int kDefaultProviderTimeoutSec = 30;

/// Default tool execution timeout
inline constexpr int kDefaultToolTimeoutSec = 30;

/// Default MCP server request timeout
inline constexpr int kDefaultMcpTimeoutSec = 30;

}  // namespace quantclaw
