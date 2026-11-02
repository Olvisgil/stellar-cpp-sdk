// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file json.hpp
/// Minimal strict JSON value/parser/serializer for RPC payloads.
/// Deliberately small: null/bool/int64/double/string/array/object, ordered
/// object keys, strict RFC 8259 numbers and escapes, duplicate-key rejection.

#include "stellar/export.hpp"
#include "stellar/result.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1 {

class STELLAR_API Json {
 public:
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  Json() : v_(nullptr) {}
  Json(std::nullptr_t) : v_(nullptr) {}
  Json(bool b) : v_(b) {}
  Json(int i) : v_(static_cast<std::int64_t>(i)) {}
  Json(std::int64_t i) : v_(i) {}
  Json(std::uint64_t u) : v_(static_cast<std::int64_t>(u)) {}
  Json(double d) : v_(d) {}
  Json(std::string s) : v_(std::move(s)) {}
  Json(const char* s) : v_(std::string(s)) {}
  Json(Array a) : v_(std::move(a)) {}
  Json(Object o) : v_(std::move(o)) {}

  static Json object() { return Json(Object{}); }
  static Json array() { return Json(Array{}); }

  enum class Kind { kNull, kBool, kInt, kDouble, kString, kArray, kObject };
  [[nodiscard]] Kind kind() const noexcept { return static_cast<Kind>(v_.index()); }
  [[nodiscard]] bool is_null() const noexcept { return kind() == Kind::kNull; }
  [[nodiscard]] bool is_bool() const noexcept { return kind() == Kind::kBool; }
  [[nodiscard]] bool is_number() const noexcept {
    return kind() == Kind::kInt || kind() == Kind::kDouble;
  }
  [[nodiscard]] bool is_string() const noexcept { return kind() == Kind::kString; }
  [[nodiscard]] bool is_array() const noexcept { return kind() == Kind::kArray; }
  [[nodiscard]] bool is_object() const noexcept { return kind() == Kind::kObject; }

  [[nodiscard]] bool as_bool() const noexcept { return std::get<1>(v_); }
  [[nodiscard]] std::int64_t as_int() const noexcept {
    return kind() == Kind::kDouble ? static_cast<std::int64_t>(std::get<3>(v_)) : std::get<2>(v_);
  }
  [[nodiscard]] double as_double() const noexcept {
    return kind() == Kind::kInt ? static_cast<double>(std::get<2>(v_)) : std::get<3>(v_);
  }
  [[nodiscard]] const std::string& as_string() const noexcept { return std::get<4>(v_); }
  [[nodiscard]] const Array& as_array() const noexcept { return std::get<5>(v_); }
  [[nodiscard]] const Object& as_object() const noexcept { return std::get<6>(v_); }

  Array& as_array_mut() noexcept { return std::get<5>(v_); }
  Object& as_object_mut() noexcept { return std::get<6>(v_); }

  /// Object member access; nullptr when missing or not an object.
  [[nodiscard]] const Json* find(std::string_view key) const noexcept;
  [[nodiscard]] const Json& at(std::string_view key) const noexcept;  // null when missing
  [[nodiscard]] const Json& at(std::size_t i) const noexcept;         // null when OOB

  void set(std::string key, Json v) { as_object_mut().emplace(std::move(key), std::move(v)); }
  void push(Json v) { as_array_mut().push_back(std::move(v)); }

  static Result<Json> parse(std::string_view text);
  [[nodiscard]] std::string dump() const;

  bool operator==(const Json&) const = default;

 private:
  std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> v_;
};

/// JSON-RPC 2.0 helpers shared by the RPC client.
struct STELLAR_API JsonRpc {
  static Json request(std::string_view method, Json params, std::int64_t id);
  /// Returns error JSON {code,message} for a JSON-RPC error response, else null.
  static const Json* error_of(const Json& response);
  static const Json* result_of(const Json& response);
};

}  // namespace stellar::inline v1
