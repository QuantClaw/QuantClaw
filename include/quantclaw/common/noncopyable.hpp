// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace quantclaw {

// Base class that disables copy construction and copy assignment.
// Move semantics are preserved — subclasses may still define move
// constructors/assignment operators if they need them.
//
// Usage:
//   class MyService : public Noncopyable { ... };
class Noncopyable {
 public:
  Noncopyable() = default;
  ~Noncopyable() = default;

  Noncopyable(const Noncopyable&) = delete;
  Noncopyable& operator=(const Noncopyable&) = delete;

  Noncopyable(Noncopyable&&) = default;
  Noncopyable& operator=(Noncopyable&&) = default;
};

}  // namespace quantclaw
