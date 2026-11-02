// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file scval.hpp
/// Ergonomic constructors and readers over xdr::SCVal, plus the protocol's
/// canonical SCVal ordering (needed for map keys and deterministic compares).

#include "stellar/core/address.hpp"
#include "stellar/core/bigint.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"
#include "stellar/xdr/generated/stellar_contract.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1 {

/// Fluent SCVal construction — mirrors every arm that's useful to callers.
struct STELLAR_API scval {
  static xdr::SCVal void_() { return xdr::SCVal::scvVoid(); }
  static xdr::SCVal bool_(bool v) { return xdr::SCVal::scvBool(v); }
  static xdr::SCVal u32(std::uint32_t v) { return xdr::SCVal::scvU32(v); }
  static xdr::SCVal i32(std::int32_t v) { return xdr::SCVal::scvI32(v); }
  static xdr::SCVal u64(std::uint64_t v) { return xdr::SCVal::scvU64(static_cast<xdr::uint64>(v)); }
  static xdr::SCVal i64(std::int64_t v) { return xdr::SCVal::scvI64(static_cast<xdr::int64>(v)); }
  static xdr::SCVal timepoint(std::uint64_t t) {
    return xdr::SCVal::scvTimepoint(static_cast<xdr::uint64>(t));
  }
  static xdr::SCVal duration(std::uint64_t d) {
    return xdr::SCVal::scvDuration(static_cast<xdr::uint64>(d));
  }
  static xdr::SCVal u128(std::uint64_t hi, std::uint64_t lo);
  static xdr::SCVal i128(std::int64_t hi, std::uint64_t lo);
  static xdr::SCVal u256(std::uint64_t hi_hi, std::uint64_t hi_lo, std::uint64_t lo_hi,
                         std::uint64_t lo_lo);
  static xdr::SCVal bytes(ByteSpan v);
  static xdr::SCVal symbol(std::string_view s);
  static xdr::SCVal string(std::string_view s);
  static xdr::SCVal address(const Address& a);
  static xdr::SCVal vec(std::vector<xdr::SCVal> items);
  static xdr::SCVal map(std::vector<xdr::SCMapEntry> entries);
  static xdr::SCVal nonce_key(std::int64_t nonce) {
    return xdr::SCVal::scvLedgerKeyNonce(xdr::SCNonceKey{static_cast<xdr::int64>(nonce)});
  }
  static xdr::SCVal ledger_key_contract_instance() {
    return xdr::SCVal::scvLedgerKeyContractInstance();
  }
  static xdr::SCVal executable_tag(std::string_view tag);  // e.g. "ContractCodeEntry"
};

/// Typed extraction (returns null/Error on type mismatch rather than
/// producing garbage).
namespace scval_get {
STELLAR_API const bool* bool_(const xdr::SCVal& v);
STELLAR_API const std::uint32_t* u32(const xdr::SCVal& v);
STELLAR_API const std::int32_t* i32(const xdr::SCVal& v);
STELLAR_API const std::uint64_t* u64(const xdr::SCVal& v);
STELLAR_API const std::int64_t* i64(const xdr::SCVal& v);
STELLAR_API const xdr::SCBytes* bytes(const xdr::SCVal& v);
STELLAR_API const xdr::SCString* string(const xdr::SCVal& v);
STELLAR_API const xdr::SCSymbol* symbol(const xdr::SCVal& v);
STELLAR_API const xdr::SCAddress* address(const xdr::SCVal& v);
STELLAR_API const xdr::SCVec* vec(const xdr::SCVal& v);
STELLAR_API const xdr::SCMap* map(const xdr::SCVal& v);
STELLAR_API const xdr::SCError* error(const xdr::SCVal& v);
}  // namespace scval_get

/// Protocol-canonical SCVal ordering: discriminant first, then arm content.
/// SCVec/SCMap compare elementwise. Mirrors stellar-core's `scval_lt` /
/// the ordering `SCMap` relies on.
STELLAR_API int scval_compare(const xdr::SCVal& a, const xdr::SCVal& b);
STELLAR_API bool scval_lt(const xdr::SCVal& a, const xdr::SCVal& b);

/// Render an SCVal readably — every arm covered recursively
/// (map entries as "k: v", vec as "[a, b]", addresses via strkey).
STELLAR_API std::string scval_to_string(const xdr::SCVal& v);

}  // namespace stellar::inline v1
