// Copyright 2024 QuantClaw Authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/curl_raii.hpp"

#include <curl/curl.h>

namespace quantclaw {

// --- CurlHandle ---

CurlHandle::CurlHandle() : handle_(curl_easy_init()) {
  if (!handle_) {
    throw std::runtime_error("Failed to initialize CURL");
  }
}

CurlHandle::~CurlHandle() {
  if (handle_) {
    curl_easy_cleanup(handle_);
  }
}

CurlHandle::CurlHandle(CurlHandle&& other) noexcept : handle_(other.handle_) {
  other.handle_ = nullptr;
}

CurlHandle& CurlHandle::operator=(CurlHandle&& other) noexcept {
  if (this != &other) {
    if (handle_) {
      curl_easy_cleanup(handle_);
    }
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

// --- CurlSlist ---

CurlSlist::~CurlSlist() {
  if (list_) {
    curl_slist_free_all(list_);
  }
}

CurlSlist::CurlSlist(CurlSlist&& other) noexcept : list_(other.list_) {
  other.list_ = nullptr;
}

CurlSlist& CurlSlist::operator=(CurlSlist&& other) noexcept {
  if (this != &other) {
    if (list_) {
      curl_slist_free_all(list_);
    }
    list_ = other.list_;
    other.list_ = nullptr;
  }
  return *this;
}

void CurlSlist::append(const char* str) {
  list_ = curl_slist_append(list_, str);
}

}  // namespace quantclaw
