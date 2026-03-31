// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <boost/ut.hpp>

#include <cmath>
#include <cstring>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>

namespace testing {

class Test {
 protected:
  virtual void SetUp() {}
  virtual void TearDown() {}

 public:
  virtual ~Test() = default;
};

}  // namespace testing

namespace testing::internal {

class FailureBuilder {
 public:
  FailureBuilder() = default;
  FailureBuilder(const FailureBuilder&) = delete;
  FailureBuilder& operator=(const FailureBuilder&) = delete;
  ~FailureBuilder() noexcept(false) {
    throw std::runtime_error(message_.empty() ? "FAIL" : message_);
  }

  template <typename T>
  FailureBuilder& operator<<(const T& value) {
    std::ostringstream stream;
    stream << value;
    message_ += stream.str();
    return *this;
  }

 private:
  std::string message_;
};

class AssertionBuilder {
 public:
  AssertionBuilder(bool failed, const char* message)
      : failed_(failed), message_(failed ? message : "") {}

  AssertionBuilder(const AssertionBuilder&) = delete;
  AssertionBuilder& operator=(const AssertionBuilder&) = delete;

  ~AssertionBuilder() noexcept(false) {
    if (failed_) {
      throw std::runtime_error(message_.empty() ? "Assertion failed" : message_);
    }
  }

  template <typename T>
  AssertionBuilder& operator<<(const T& value) {
    if (!failed_) {
      return *this;
    }
    if (!has_extra_) {
      message_ += ": ";
      has_extra_ = true;
    }
    std::ostringstream stream;
    stream << value;
    message_ += stream.str();
    return *this;
  }

 private:
  bool failed_;
  bool has_extra_ = false;
  std::string message_;
};

}  // namespace testing::internal

#define QC_GTEST_CONCAT_INNER(a, b) a##b
#define QC_GTEST_CONCAT(a, b) QC_GTEST_CONCAT_INNER(a, b)

#define TEST(SuiteName, TestName)                                             \
  struct QC_GTEST_CONCAT(SuiteName, QC_GTEST_CONCAT(_, TestName)) {           \
    void Run();                                                                \
  };                                                                           \
  static const auto QC_GTEST_CONCAT(                                          \
      QC_GTEST_CONCAT(SuiteName, QC_GTEST_CONCAT(_, TestName)),                \
      _registration) = [] {                                                    \
    ::boost::ext::ut::v2_3_1::test(#SuiteName "." #TestName) = [=] {         \
      QC_GTEST_CONCAT(SuiteName, QC_GTEST_CONCAT(_, TestName)) test_case;      \
      test_case.Run();                                                         \
    };                                                                         \
    return 0;                                                                  \
  }();                                                                         \
  void QC_GTEST_CONCAT(SuiteName, QC_GTEST_CONCAT(_, TestName))::Run()

#define TEST_F(Fixture, TestName)                                              \
  struct QC_GTEST_CONCAT(Fixture, QC_GTEST_CONCAT(_, TestName))                \
      : public Fixture {                                                       \
    void Body();                                                               \
    void Run() {                                                               \
      this->SetUp();                                                           \
      auto _qc_teardown = [this]() { this->TearDown(); };                     \
      struct _qc_guard {                                                       \
        decltype(_qc_teardown)& fn;                                            \
        ~_qc_guard() { fn(); }                                                 \
      } _qc_guard_instance{_qc_teardown};                                      \
      Body();                                                                  \
    }                                                                          \
  };                                                                           \
  static const auto QC_GTEST_CONCAT(                                          \
      QC_GTEST_CONCAT(Fixture, QC_GTEST_CONCAT(_, TestName)),                  \
      _registration) = [] {                                                    \
    ::boost::ext::ut::v2_3_1::test(#Fixture "." #TestName) = [=] {           \
      QC_GTEST_CONCAT(Fixture, QC_GTEST_CONCAT(_, TestName)) test_case;        \
      test_case.Run();                                                         \
    };                                                                         \
    return 0;                                                                  \
  }();                                                                         \
  void QC_GTEST_CONCAT(Fixture, QC_GTEST_CONCAT(_, TestName))::Body()

#define EXPECT_TRUE(condition)                                                \
  ::testing::internal::AssertionBuilder(!(condition), "EXPECT_TRUE failed")
#define EXPECT_FALSE(condition)                                               \
  ::testing::internal::AssertionBuilder((condition), "EXPECT_FALSE failed")
#define EXPECT_EQ(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) == (rhs)), "EXPECT_EQ failed")
#define EXPECT_NE(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) != (rhs)), "EXPECT_NE failed")
#define EXPECT_LT(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) < (rhs)), "EXPECT_LT failed")
#define EXPECT_LE(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) <= (rhs)), "EXPECT_LE failed")
#define EXPECT_GT(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) > (rhs)), "EXPECT_GT failed")
#define EXPECT_GE(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) >= (rhs)), "EXPECT_GE failed")
#define EXPECT_DOUBLE_EQ(lhs, rhs)                                            \
  ::testing::internal::AssertionBuilder(!((lhs) == (rhs)),                    \
                                        "EXPECT_DOUBLE_EQ failed")
#define EXPECT_NEAR(lhs, rhs, abs_error)                                      \
  ::testing::internal::AssertionBuilder(                                      \
      !(std::fabs((lhs) - (rhs)) <= (abs_error)), "EXPECT_NEAR failed")
#define EXPECT_STREQ(lhs, rhs)                                                \
  ::testing::internal::AssertionBuilder(!(std::strcmp((lhs), (rhs)) == 0),    \
                                        "EXPECT_STREQ failed")
#define EXPECT_STRNE(lhs, rhs)                                                \
  ::testing::internal::AssertionBuilder(!(std::strcmp((lhs), (rhs)) != 0),    \
                                        "EXPECT_STRNE failed")
#define EXPECT_THROW(statement, expected_exception)                           \
  ([&]() -> ::testing::internal::AssertionBuilder {                           \
    bool _qc_threw = false;                                                   \
    try {                                                                     \
      statement;                                                              \
    } catch (const expected_exception&) {                                     \
      _qc_threw = true;                                                       \
    } catch (...) {                                                           \
      throw;                                                                  \
    }                                                                         \
    return ::testing::internal::AssertionBuilder(!_qc_threw,                  \
                                                 "EXPECT_THROW failed");     \
  }())
#define EXPECT_ANY_THROW(statement)                                           \
  ([&]() -> ::testing::internal::AssertionBuilder {                           \
    bool _qc_threw = false;                                                   \
    try {                                                                     \
      statement;                                                              \
    } catch (...) {                                                           \
      _qc_threw = true;                                                       \
    }                                                                         \
    return ::testing::internal::AssertionBuilder(!_qc_threw,                  \
                                                 "EXPECT_ANY_THROW failed"); \
  }())
#define EXPECT_NO_THROW(statement)                                            \
  ([&]() -> ::testing::internal::AssertionBuilder {                           \
    bool _qc_threw = false;                                                   \
    try {                                                                     \
      statement;                                                              \
    } catch (...) {                                                           \
      _qc_threw = true;                                                       \
    }                                                                         \
    return ::testing::internal::AssertionBuilder(_qc_threw,                   \
                                                 "EXPECT_NO_THROW failed");  \
  }())
#define EXPECT_NO_FATAL_FAILURE(statement)                                    \
  ([&]() -> ::testing::internal::AssertionBuilder {                           \
    statement;                                                                \
    return ::testing::internal::AssertionBuilder(false, "");                 \
  }())

#define ASSERT_TRUE(condition)                                                \
  ::testing::internal::AssertionBuilder(!(condition), "ASSERT_TRUE failed")
#define ASSERT_FALSE(condition)                                               \
  ::testing::internal::AssertionBuilder((condition), "ASSERT_FALSE failed")
#define ASSERT_EQ(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) == (rhs)), "ASSERT_EQ failed")
#define ASSERT_NE(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) != (rhs)), "ASSERT_NE failed")
#define ASSERT_LT(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) < (rhs)), "ASSERT_LT failed")
#define ASSERT_LE(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) <= (rhs)), "ASSERT_LE failed")
#define ASSERT_GT(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) > (rhs)), "ASSERT_GT failed")
#define ASSERT_GE(lhs, rhs)                                                   \
  ::testing::internal::AssertionBuilder(!((lhs) >= (rhs)), "ASSERT_GE failed")
#define ASSERT_THROW(statement, expected_exception)                           \
  ([&]() -> ::testing::internal::AssertionBuilder {                           \
    bool _qc_threw = false;                                                   \
    try {                                                                     \
      statement;                                                              \
    } catch (const expected_exception&) {                                     \
      _qc_threw = true;                                                       \
    } catch (...) {                                                           \
      throw;                                                                  \
    }                                                                         \
    return ::testing::internal::AssertionBuilder(!_qc_threw,                  \
                                                 "ASSERT_THROW failed");     \
  }())
#define ASSERT_ANY_THROW(statement)                                           \
  ([&]() -> ::testing::internal::AssertionBuilder {                           \
    bool _qc_threw = false;                                                   \
    try {                                                                     \
      statement;                                                              \
    } catch (...) {                                                           \
      _qc_threw = true;                                                       \
    }                                                                         \
    return ::testing::internal::AssertionBuilder(!_qc_threw,                  \
                                                 "ASSERT_ANY_THROW failed"); \
  }())
#define ASSERT_NO_THROW(statement)                                            \
  ([&]() -> ::testing::internal::AssertionBuilder {                           \
    bool _qc_threw = false;                                                   \
    try {                                                                     \
      statement;                                                              \
    } catch (...) {                                                           \
      _qc_threw = true;                                                       \
    }                                                                         \
    return ::testing::internal::AssertionBuilder(_qc_threw,                   \
                                                 "ASSERT_NO_THROW failed");  \
  }())

#define FAIL() ::testing::internal::FailureBuilder()
