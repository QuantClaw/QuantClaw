// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <type_traits>
#include <utility>

// ─── QC_TRY / QC_TRY_ASSIGN ────────────────────────────────────────────────
//
// Propagates errors up the call stack, similar to Rust's `?` operator.
//
// Given an expression that returns a StatusOr<T> (or any type that is
// contextually convertible to bool for "is OK" and supports operator* for
// value extraction), QC_TRY either:
//   • returns the error from the enclosing function if the result is falsy, or
//   • yields the unwrapped success value if truthy.
//
// Two variants:
//
//   QC_TRY_ASSIGN(var, expr)   — Cross-platform (GCC/Clang/MSVC).
//                                 Declares `var` with the unwrapped value.
//     QC_TRY_ASSIGN(val, ComputeSomething(x));   // val is now an int
//     QC_TRY_ASSIGN(data, LoadFile(path));        // data is now Bytes
//
//   QC_TRY(expr)               — GCC/Clang only (statement expression).
//                                 Can be used inline as an expression.
//     auto val = QC_TRY(ComputeSomething(x));

namespace quantclaw::qc_detail {

template <typename T>
struct unwrap_result {
  // Return by value (not decltype(auto)) to prevent dangling references
  // when used in GCC statement expressions where the source is destroyed.
  static auto
  get(T&& v) -> std::remove_reference_t<decltype(*std::forward<T>(v))> {
    return *std::forward<T>(v);
  }
};

}  // namespace quantclaw::qc_detail

// ── Cross-platform: QC_TRY_ASSIGN(var, expr) ────────────────────────────────
// Works on GCC, Clang, and MSVC.  Declares `var` in the enclosing scope.
#define QC_TRY_CONCAT_IMPL(a, b) a##b
#define QC_TRY_CONCAT(a, b) QC_TRY_CONCAT_IMPL(a, b)

#define QC_TRY_ASSIGN(var, ...)                                          \
  auto QC_TRY_CONCAT(_qc_try_r_, __LINE__) = (__VA_ARGS__);              \
  if (!static_cast<bool>(QC_TRY_CONCAT(_qc_try_r_, __LINE__)))           \
    return std::forward<decltype(QC_TRY_CONCAT(_qc_try_r_, __LINE__))>(  \
        QC_TRY_CONCAT(_qc_try_r_, __LINE__));                            \
  auto var = ::quantclaw::qc_detail::                                    \
      unwrap_result<decltype(QC_TRY_CONCAT(_qc_try_r_, __LINE__))>::get( \
          std::forward<decltype(QC_TRY_CONCAT(_qc_try_r_, __LINE__))>(   \
              QC_TRY_CONCAT(_qc_try_r_, __LINE__)))

// ── GCC/Clang only: QC_TRY(expr) ────────────────────────────────────────────
// Usable as an expression: auto val = QC_TRY(expr);
#if defined(__GNUC__) || defined(__clang__)
#define QC_TRY(...)                                            \
  __extension__({                                              \
    auto _r_ = (__VA_ARGS__);                                  \
    if (!static_cast<bool>(_r_))                               \
      return std::forward<decltype(_r_)>(_r_);                 \
    ::quantclaw::qc_detail::unwrap_result<decltype(_r_)>::get( \
        std::forward<decltype(_r_)>(_r_));                     \
  })
#endif
