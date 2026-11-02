// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file bigint.hpp
/// Portable fixed-width integers matching the XDR int128/uint128/int256/
/// uint256 shapes (arrays of bytes/limbs in XDR order — big-endian). Pure
/// byte-carrying value types: conversion to/from XDR, hex, compare. Arithmetic
/// is intentionally minimal (no claim to be a bignum library).

#include "stellar/export.hpp"
#include "stellar/result.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace stellar::inline v1 {

/// A fixed-width big-endian integer container. Two's complement for Signed.
/// Arithmetic wraps at 2^(8N) like the on-ledger fixed-width values.
template <std::size_t N, bool Signed>
class BigInt {
 public:
  std::array<std::uint8_t, N> bytes{};

  static Result<BigInt> from_hex(std::string_view hex);
  /// Decimal string ("-12345"). Errors on non-digits or range overflow.
  static Result<BigInt> from_decimal(std::string_view dec);
  [[nodiscard]] std::string to_hex() const;
  [[nodiscard]] std::string to_decimal() const;
  [[nodiscard]] bool negative() const noexcept { return Signed && (bytes[0] & 0x80); }
  bool operator==(const BigInt&) const = default;
  bool operator<(const BigInt& o) const noexcept {
    if constexpr (Signed) {
      // two's complement: sign bit dominates, then lexicographic within a sign
      if (negative() != o.negative()) return negative();
    }
    return bytes < o.bytes;
  }
  bool operator<=(const BigInt& o) const noexcept { return !(o < *this); }
  bool operator>(const BigInt& o) const noexcept { return o < *this; }
  bool operator>=(const BigInt& o) const noexcept { return !(*this < o); }

  /// Wrapping two's-complement arithmetic (mod 2^(8N)).
  BigInt operator+(const BigInt& o) const noexcept;
  BigInt operator-(const BigInt& o) const noexcept;
  BigInt operator*(const BigInt& o) const noexcept;
  /// Two's-complement negation; -0 = 0, -MIN = MIN (wraps).
  BigInt operator-() const noexcept;
};

using Int128 = BigInt<16, true>;
using UInt128 = BigInt<16, false>;
using Int256 = BigInt<32, true>;
using UInt256 = BigInt<32, false>;

extern template class BigInt<16, true>;
extern template class BigInt<16, false>;
extern template class BigInt<32, true>;
extern template class BigInt<32, false>;

}  // namespace stellar::inline v1
