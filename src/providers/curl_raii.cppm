// Copyright 2024 QuantClaw Authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

module;
#include <curl/curl.h>

export module quantclaw.providers.curl_raii;

import std;

export namespace quantclaw {

class CurlHandle {
 public:
  CurlHandle();
  ~CurlHandle();

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

class CurlSlist {
 public:
  CurlSlist() = default;
  ~CurlSlist();

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