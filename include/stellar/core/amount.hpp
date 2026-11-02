// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file amount.hpp
/// Stroop-precise amounts: int64 stroops with exact 7-decimal parse/format.
/// Never floating point.

#include "stellar/export.hpp"
#include "stellar/result.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace stellar::inline v1 {

class STELLAR_API Amount {
 public:
  static constexpr std::int64_t kOne = 10'000'000;            // stroops per unit
  static constexpr std::int64_t kMaxStroop = INT64_MAX / 10;  // safety rail vs protocol int64

  explicit constexpr Amount(std::int64_t stroops = 0) : stroops_(stroops) {}

  /// Parses "123.4567890" (max 7 decimals, optional sign). Rejects fractions
  /// beyond 7 decimals, empty strings, and overflow.
  static Result<Amount> parse(std::string_view text);

  /// Canonical "X.Y" form (trailing zeros kept to 7dp when fractional input
  /// had them, else minimal). Always exact.
  [[nodiscard]] std::string to_string() const;

  [[nodiscard]] std::int64_t stroops() const noexcept { return stroops_; }
  [[nodiscard]] bool positive() const noexcept { return stroops_ > 0; }

  bool operator==(const Amount&) const = default;
  auto operator<=>(const Amount&) const = default;

 private:
  std::int64_t stroops_;
};

}  // namespace stellar::inline v1
