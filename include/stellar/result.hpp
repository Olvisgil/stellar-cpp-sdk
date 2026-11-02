// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file result.hpp
/// stellar::Result<T> — expected-style return type used throughout the SDK.
///
/// The SDK never throws from its own API surface: recoverable failures are
/// returned as Result. Result works in -fno-exceptions builds; only
/// Result::value() throws (when exceptions are enabled) or aborts (when not).

#include "stellar/error.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define STELLAR_EXCEPTIONS_ENABLED 1
#else
#define STELLAR_EXCEPTIONS_ENABLED 0
#endif

namespace stellar::inline v1 {

#if STELLAR_EXCEPTIONS_ENABLED
/// Thrown by Result::value() when the Result holds an error.
class ErrorException : public std::exception {
 public:
  explicit ErrorException(Error error) : error_(std::move(error)), what_(error_.describe()) {}
  [[nodiscard]] const char* what() const noexcept override { return what_.c_str(); }
  [[nodiscard]] const Error& error() const noexcept { return error_; }

 private:
  Error error_;
  std::string what_;
};
#endif

namespace detail {

[[noreturn]] inline void fail_value_access(const Error& error) {
#if STELLAR_EXCEPTIONS_ENABLED
  throw ErrorException(error);
#else
  std::fprintf(stderr, "stellar::Result::value() on error: %s\n", error.describe().c_str());
  std::abort();
#endif
}

}  // namespace detail

/// A value of type T or a stellar::Error.
template <typename T>
class [[nodiscard]] Result {
 public:
  using value_type = T;

  Result(const T& value) : data_(value) {}
  Result(T&& value) : data_(std::move(value)) {}
  Result(const Error& error) : data_(error) {}
  Result(Error&& error) : data_(std::move(error)) {}

  [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }
  explicit operator bool() const noexcept { return ok(); }

  [[nodiscard]] const T& value() const& {
    if (!ok()) detail::fail_value_access(std::get<Error>(data_));
    return std::get<T>(data_);
  }
  [[nodiscard]] T& value() & {
    if (!ok()) detail::fail_value_access(std::get<Error>(data_));
    return std::get<T>(data_);
  }
  [[nodiscard]] T&& value() && {
    if (!ok()) detail::fail_value_access(std::get<Error>(data_));
    return std::move(std::get<T>(data_));
  }

  const T* operator->() const { return &value(); }
  T* operator->() { return &value(); }
  const T& operator*() const& { return value(); }
  T& operator*() & { return value(); }
  T&& operator*() && { return std::move(value()); }

  [[nodiscard]] const Error& error() const& { return std::get<Error>(data_); }
  [[nodiscard]] Error&& error() && { return std::move(std::get<Error>(data_)); }

  [[nodiscard]] T value_or(T fallback) const& { return ok() ? std::get<T>(data_) : std::move(fallback); }
  [[nodiscard]] T value_or(T fallback) && {
    return ok() ? std::move(std::get<T>(data_)) : std::move(fallback);
  }

  /// f(T) -> Result<U>; propagates the error.
  template <typename F>
  auto and_then(F&& f) & -> decltype(std::forward<F>(f)(std::declval<T&>())) {
    using Out = decltype(std::forward<F>(f)(std::declval<T&>()));
    if (!ok()) return Out{std::get<Error>(data_)};
    return std::forward<F>(f)(std::get<T>(data_));
  }
  template <typename F>
  auto and_then(F&& f) && -> decltype(std::forward<F>(f)(std::declval<T&&>())) {
    using Out = decltype(std::forward<F>(f)(std::declval<T&&>()));
    if (!ok()) return Out{std::move(std::get<Error>(data_))};
    return std::forward<F>(f)(std::move(std::get<T>(data_)));
  }
  template <typename F>
  auto and_then(F&& f) const& -> decltype(std::forward<F>(f)(std::declval<const T&>())) {
    using Out = decltype(std::forward<F>(f)(std::declval<const T&>()));
    if (!ok()) return Out{std::get<Error>(data_)};
    return std::forward<F>(f)(std::get<T>(data_));
  }

  /// f(T) -> U; wraps the produced value in Result<U>.
  template <typename F>
  auto transform(F&& f) & -> Result<decltype(std::forward<F>(f)(std::declval<T&>()))> {
    using U = decltype(std::forward<F>(f)(std::declval<T&>()));
    if (!ok()) return Result<U>{std::get<Error>(data_)};
    return Result<U>{std::forward<F>(f)(std::get<T>(data_))};
  }
  template <typename F>
  auto transform(F&& f) && -> Result<decltype(std::forward<F>(f)(std::declval<T&&>()))> {
    using U = decltype(std::forward<F>(f)(std::declval<T&&>()));
    if (!ok()) return Result<U>{std::move(std::get<Error>(data_))};
    return Result<U>{std::forward<F>(f)(std::move(std::get<T>(data_)))};
  }

  /// f(Error) -> Result<T>; error recovery.
  template <typename F>
  Result or_else(F&& f) const& {
    if (ok()) return *this;
    return std::forward<F>(f)(std::get<Error>(data_));
  }
  template <typename F>
  Result or_else(F&& f) && {
    if (ok()) return std::move(*this);
    return std::forward<F>(f)(std::move(std::get<Error>(data_)));
  }

  /// Adds an outer context layer to the error, if any.
  template <typename S>
  Result with_context(S&& layer) && {
    if (!ok()) data_ = std::move(std::get<Error>(data_)).with_context(std::forward<S>(layer));
    return std::move(*this);
  }

  bool operator==(const Result&) const = default;

 private:
  std::variant<T, Error> data_;
};

/// A void success or an error.
template <>
class [[nodiscard]] Result<void> {
 public:
  Result() = default;
  Result(const Error& error) : error_(error) {}
  Result(Error&& error) : error_(std::move(error)) {}

  template <typename T>
  Result(const Result<T>& other) {  // NOLINT(google-explicit-constructor) - drops value, keeps error
    if (!other.ok()) error_ = other.error();
  }

  [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
  explicit operator bool() const noexcept { return ok(); }

  void value() const {
    if (!ok()) detail::fail_value_access(*error_);
  }

  [[nodiscard]] const Error& error() const& { return *error_; }
  [[nodiscard]] Error&& error() && { return std::move(*error_); }

  template <typename S>
  Result with_context(S&& layer) && {
    if (error_) error_ = std::move(*error_).with_context(std::forward<S>(layer));
    return std::move(*this);
  }

  bool operator==(const Result&) const = default;

 private:
  std::optional<Error> error_;
};

#define STELLAR_DETAIL_CONCAT_INNER(a, b) a##b
#define STELLAR_DETAIL_CONCAT(a, b) STELLAR_DETAIL_CONCAT_INNER(a, b)

/// Propagates an error out of a function returning Result<T>/Result<void>.
///
///   STELLAR_TRY_ASSIGN(const KeyPair kp, KeyPair::from_secret(seed));
///   STELLAR_TRY(rpc.send_and_wait(tx));
#define STELLAR_TRY_ASSIGN(decl, expr)                                                    \
  auto STELLAR_DETAIL_CONCAT(_stellar_result_, __LINE__) = (expr);                        \
  if (!STELLAR_DETAIL_CONCAT(_stellar_result_, __LINE__).ok()) {                          \
    return std::move(STELLAR_DETAIL_CONCAT(_stellar_result_, __LINE__)).error();          \
  }                                                                                     \
  decl = std::move(STELLAR_DETAIL_CONCAT(_stellar_result_, __LINE__)).value()

#define STELLAR_TRY(expr)                                                                 \
  {                                                                                       \
    auto STELLAR_DETAIL_CONCAT(_stellar_result_, __LINE__) = (expr);                        \
    if (!STELLAR_DETAIL_CONCAT(_stellar_result_, __LINE__).ok()) {                          \
      return std::move(STELLAR_DETAIL_CONCAT(_stellar_result_, __LINE__)).error();          \
    }                                                                                     \
  }

}  // namespace stellar::inline v1
