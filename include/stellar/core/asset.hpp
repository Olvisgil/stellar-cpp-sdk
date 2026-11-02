// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file asset.hpp
/// Assets: native XLM plus credit alphanum4/alphanum12, canonical
/// "CODE:ISSUER" parsing, ChangeTrustAsset/TrustLineAsset conversion,
/// liquidity-pool ID derivation, Stellar Asset Contract (SAC) contract IDs.

#include "stellar/core/strkey.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"
#include "stellar/xdr/generated/stellar_ledger_entries.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace stellar::inline v1 {

class STELLAR_API Asset {
 public:
  enum class Kind { kNative, kAlphaNum4, kAlphaNum12 };

  static Asset native();
  /// code: 1-4 chars for AlphaNum4, 5-12 for AlphaNum12; issuer: G... strkey.
  static Result<Asset> credit(std::string_view code, std::string_view issuer);
  /// "native" or "CODE:ISSUER".
  static Result<Asset> parse(std::string_view text);
  static Result<Asset> from_xdr(const xdr::Asset& a);

  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  [[nodiscard]] bool is_native() const noexcept { return kind_ == Kind::kNative; }
  [[nodiscard]] const std::string& code() const noexcept { return code_; }
  [[nodiscard]] const std::string& issuer() const noexcept { return issuer_; }
  /// "native" or "CODE:ISSUER".
  [[nodiscard]] std::string to_string() const;

  [[nodiscard]] Result<xdr::Asset> to_xdr() const;
  [[nodiscard]] Result<xdr::ChangeTrustAsset> to_change_trust_asset() const;
  [[nodiscard]] Result<xdr::TrustLineAsset> to_trust_line_asset() const;

  /// LP ID for the pool of this asset and `other` at fee 30bps: SHA-256 of the
  /// canonical LiquidityPoolParameters XDR (type + assetA + assetB + fee),
  /// assets ordered by protocol comparison.
  static Result<std::array<std::uint8_t, 32>> liquidity_pool_id(const Asset& a, const Asset& b);

  /// Protocol ordering used by path finding and pool ID derivation
  /// (native < alphanum4 < alphanum12, then code, then issuer).
  [[nodiscard]] int compare(const Asset& other) const;
  bool operator<(const Asset& o) const { return compare(o) < 0; }
  bool operator==(const Asset&) const = default;

  /// Contract ID of the SAC for this asset on `network`: SHA-256 of
  /// HashIDPreimage{ENVELOPE_TYPE_CONTRACT_ID, networkId, CONTRACT_ID_FROM_ASSET, asset}.
  [[nodiscard]] Result<std::array<std::uint8_t, 32>> sac_contract_id(const class Network& n) const;

 private:
  Kind kind_ = Kind::kNative;
  std::string code_;
  std::string issuer_;  // G... canonical strkey
};

}  // namespace stellar::inline v1
