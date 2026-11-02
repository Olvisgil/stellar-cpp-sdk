// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file address.hpp
/// Unified addresses: G (account), M (muxed account), C (contract), W (muxed
/// contract). Converts to/from the XDR SCAddress/MuxedAccount forms.

#include "stellar/core/strkey.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/xdr/generated/stellar_contract.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"
#include "stellar/xdr/generated/stellar_types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace stellar::inline v1 {

/// Any addressable thing on the network.
class STELLAR_API Address {
 public:
  enum class Kind { kAccount, kMuxedAccount, kContract, kMuxedContract };

  /// Parses a G, M, C or W strkey.
  static Result<Address> parse(std::string_view text);
  static Result<Address> from_strkey(StrKey sk);
  static Result<Address> from_sc_address(const xdr::SCAddress& a);
  static Address contract(std::array<std::uint8_t, 32> id);

  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  [[nodiscard]] std::string to_string() const;  // canonical strkey

  [[nodiscard]] Result<xdr::SCAddress> to_sc_address() const;
  [[nodiscard]] Result<xdr::MuxedAccount> to_muxed_account() const;  // G/M only
  [[nodiscard]] const std::array<std::uint8_t, 32>* account_id_bytes() const;

  /// Muxed components (G/M/W only).
  [[nodiscard]] std::optional<std::uint64_t> muxed_id() const;

  bool operator==(const Address&) const = default;

 private:
  Kind kind_;
  std::array<std::uint8_t, 32> id_{};
  std::optional<std::uint64_t> sub_id_;  // M / W only
};

}  // namespace stellar::inline v1
