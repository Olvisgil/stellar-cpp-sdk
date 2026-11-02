// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
/// @file token_client.hpp
/// SEP-41 token interface client — thin typed wrappers over ContractClient.
#include "stellar/contract/contract_client.hpp"
#include "stellar/contract/scval.hpp"
namespace stellar::inline v1 {

/// SEP-41 token client: balance/allowance/decimals/symbol reads plus
/// transfer/approve/burn/clawback invokes over a ContractClient.
class STELLAR_API TokenClient {
 public:
  TokenClient(ContractClient& client) : client_(client) {}

  // ---- reads (simulated, no signature needed) ----
  [[nodiscard]] Result<std::int64_t> balance(const Address& source, const Address& owner);
  [[nodiscard]] Result<std::int64_t> allowance(const Address& source, const Address& from, const Address& spender);
  [[nodiscard]] Result<std::int64_t> decimals(const Address& source);
  [[nodiscard]] Result<std::string> name(const Address& source);
  [[nodiscard]] Result<std::string> symbol(const Address& source);
  [[nodiscard]] Result<std::int64_t> total_supply(const Address& source);
  [[nodiscard]] Result<std::int64_t> burnable(const Address& source, const Address& who);

  // ---- writes (sign + submit) ----
  [[nodiscard]] Result<TransactionResponse>
  transfer(const Address& source, std::int64_t seq, const Signer& signer,
           const Address& from, const Address& to, std::int64_t amount);
  [[nodiscard]] Result<TransactionResponse>
  approve(const Address& source, std::int64_t seq, const Signer& signer,
          const Address& from, const Address& spender, std::int64_t amount,
          std::uint32_t expiration_ledger);
  [[nodiscard]] Result<TransactionResponse>
  burn(const Address& source, std::int64_t seq, const Signer& signer,
       const Address& from, std::int64_t amount);
  [[nodiscard]] Result<TransactionResponse>
  clawback(const Address& source, std::int64_t seq, const Signer& signer,
           const Address& admin, const Address& from, std::int64_t amount);

  /// Extract the destination address from an SEP-41 event value — works on
  /// both plain (G...) and muxed (W...) contract destinations. Returns the
  /// SCAddress → Address conversion for whichever arm is present.
  [[nodiscard]] static Result<Address> event_address(const xdr::SCVal& v);

  /// True if the SCVal carries a muxed (W-address) destination arm.
  [[nodiscard]] static bool is_muxed_address(const xdr::SCVal& v);

  /// W-address event helper: extract a (topic, value) pair as strings —
  /// `topic` is the event symbol ("transfer"/"approve"/...), `value` decodes
  /// per SEP-41 (address, amount). Best-effort for logs/display.
  [[nodiscard]] static Result<std::pair<std::string, std::string>>
  decode_event(const EventRow& event);

 private:
  ContractClient& client_;
};

}  // namespace stellar::inline v1
