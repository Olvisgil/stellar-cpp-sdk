// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file price.hpp
/// Prices as exact rationals with int32 numerator/denominator, matching the
/// protocol's `Price` type. Provides Stern-Brocot best rational approximation
/// for decimal input (parity with js-stellar-sdk and stellar-core's
/// bigRatOrBytes best_rational semantics: n,d in int32 range).

#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace stellar::inline v1 {

class STELLAR_API Price {
 public:
  Price(std::int32_t n, std::int32_t d);

  /// Parses a decimal ("1.25") or fraction ("5/4") into the best rational
  /// approximation representable with int32 n,d.
  static Result<Price> parse(std::string_view text);
  static Price from_xdr(const xdr::Price& p) { return Price(p.n, p.d); }

  /// Best rational approximation of an unsigned rational num/den with both
  /// sides limited to int32 — Stern-Brocot search matching stellar-core.
  static Result<Price> best_rational(std::int64_t num, std::int64_t den);

  [[nodiscard]] std::int32_t n() const noexcept { return n_; }
  [[nodiscard]] std::int32_t d() const noexcept { return d_; }
  [[nodiscard]] xdr::Price to_xdr() const { return xdr::Price{n_, d_}; }
  [[nodiscard]] std::string to_string() const;  // "n/d" reduced

  bool operator==(const Price&) const = default;

 private:
  std::int32_t n_;
  std::int32_t d_;
};

}  // namespace stellar::inline v1
