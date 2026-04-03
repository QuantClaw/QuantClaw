// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Platform typedefs for test helpers — kept in a header so the global module
// fragment only contains preprocessor inclusions (C++23 [module.global.frag]).

#ifndef QUANTCLAW_TEST_HELPERS_PLATFORM_H_
#define QUANTCLAW_TEST_HELPERS_PLATFORM_H_

#ifdef _WIN32
#include <winsock2.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

#endif  // QUANTCLAW_TEST_HELPERS_PLATFORM_H_
