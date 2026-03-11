// Copyright 2024 QuantClaw Authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// RAII wrappers for libcurl resources.

#pragma once

#include <stdexcept>

struct curl_slist;

// Forward-declare CURL as void* to avoid pulling in <curl/curl.h> in headers.
// The actual curl typedefs are only needed in .cpp files.
using CURL = void;

namespace quantclaw {

// RAII wrapper for CURL* (curl_easy handle).
class CurlHandle {
 public:
  CurlHandle();
  ~CurlHandle();

  // Non-copyable, movable.
  CurlHandle(const CurlHandle&) = delete;
  CurlHandle& operator=(const CurlHandle&) = delete;
  CurlHandle(CurlHandle&& other) noexcept;
  CurlHandle& operator=(CurlHandle&& other) noexcept;

  CURL* get() const {
    return handle_;
  }
  operator CURL*() const {
    return handle_;
  }

 private:
  CURL* handle_;
};

// RAII wrapper for curl_slist* (linked list of strings).
class CurlSlist {
 public:
  CurlSlist() = default;
  ~CurlSlist();

  // Non-copyable, movable.
  CurlSlist(const CurlSlist&) = delete;
  CurlSlist& operator=(const CurlSlist&) = delete;
  CurlSlist(CurlSlist&& other) noexcept;
  CurlSlist& operator=(CurlSlist&& other) noexcept;

  void append(const char* str);
  curl_slist* get() const {
    return list_;
  }
  operator curl_slist*() const {
    return list_;
  }

 private:
  curl_slist* list_ = nullptr;
};

}  // namespace quantclaw
