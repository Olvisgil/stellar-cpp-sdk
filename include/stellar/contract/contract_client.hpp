// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file contract_client.hpp
/// High-level Soroban contract client: contract-id derivation, host-function
/// builders, authorization signing, and invoke/simulate/assemble/deploy over
/// the RPC client.

#include "stellar/core/address.hpp"
#include "stellar/core/keypair.hpp"
#include "stellar/core/network.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/rpc/rpc_client.hpp"
#include "stellar/contract/contract_meta.hpp"
#include "stellar/contract/delegate_tree.hpp"
#include "stellar/tx/transaction.hpp"
#include "stellar/xdr/generated/stellar_contract.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1 {

/// Derivation and construction helpers that don't need an RPC connection.
struct STELLAR_API contract {
  /// CONTRACT_ID_FROM_ADDRESS preimage hash on `network`:
  /// sha256(HashIDPreimage{CONTRACT_ID, networkId, FROM_ADDRESS{SCAddress, salt}}).
  static Result<std::array<std::uint8_t, 32>> id_from_address(const Network& network,
                                                              const Address& deployer,
                                                              std::array<std::uint8_t, 32> salt);
  /// ContractId as a C... strkey (address form of the same id).
  static std::string contract_strkey(std::array<std::uint8_t, 32> id);

  /// INVOKE_CONTRACT host function: (contract SCAddress, function symbol, args).
  static Result<xdr::HostFunction> invoke_fn(std::string_view contract_strkey_or_address,
                                             std::string_view function,
                                             const std::vector<xdr::SCVal>& args);
  /// CREATE_CONTRACT host function (v1): preimage{FROM_ADDRESS{addr,salt}} +
  /// WASM executable, optional constructor args.
  static Result<xdr::HostFunction> create_contract_fn(
      const Address& deployer, std::array<std::uint8_t, 32> salt,
      const xdr::ContractExecutable& executable,
      const std::vector<xdr::SCVal>& constructor_args = {});
  /// CREATE_CONTRACT_V2 with constructor args (protocol >= 20 style).
  static Result<xdr::HostFunction> create_contract_v2_fn(
      const Address& deployer, std::array<std::uint8_t, 32> salt,
      const xdr::ContractExecutable& executable,
      const std::vector<xdr::SCVal>& constructor_args = {});
  /// UPLOAD_CONTRACT_WASM host function (raw wasm bytes).
  static xdr::HostFunction upload_wasm_fn(ByteSpan wasm);

  /// wasm executable tag (the wasm hash → ContractExecutable::wasm).
  static xdr::ContractExecutable wasm_executable(std::array<std::uint8_t, 32> wasm_hash);
  /// SAC-executable tag (create an instance bound to the Stellar Asset
  /// Contract — no wasm upload needed).
  static xdr::ContractExecutable sac_executable();

  /// Sign a Soroban authorization entry's SOROBAN_AUTHORIZED_FUNCTION of
  /// type CONTRACT_FN with a KeyPair for `address` on `network`, replacing its
  /// credentials with the signature. For SOURCE_ACCOUNT entries no signature
  /// is needed — pass them through unchanged.
  static Result<xdr::SorobanAuthorizationEntry>
  sign_auth_entry(const xdr::SorobanAuthorizationEntry& entry, const Network& network,
                  const KeyPair& kp);
};

/// High-level client binding a `ContractID` to an RPC + network.
class STELLAR_API ContractClient {
 public:
  ContractClient(RpcClient& rpc, const Network& network, std::string contract_id)
      : rpc_(rpc), network_(network), id_(std::move(contract_id)) {}

  [[nodiscard]] const std::string& contract_id() const noexcept { return id_; }

  /// Simulated (read-only) call — returns the simulated SCVal result.
  /// `source` may be a plain or muxed account; seq/fee are placeholders
  /// because simulation doesn't consume them.
  [[nodiscard]] Result<xdr::SCVal> call(const Address& source, std::string_view function,
                                        const std::vector<xdr::SCVal>& args);

  /// Options for a full invoke.
  struct InvokeOptions {
    InvokeOptions() = default;
    std::uint32_t fee;                  ///< inclusion fee (0 -> 100)
    std::uint32_t resource_leeway_pct;  ///< pad sim resourceFee by N%
    bool fee_bump;                      ///< wrap submission in fee-bump
    std::uint32_t bump_fee;             ///< total fee for the bump
    bool auto_restore;                  ///< auto-prepend restore op
    [[nodiscard]] static InvokeOptions defaults() {
      InvokeOptions o;
      o.fee = 0;
      o.resource_leeway_pct = 25;
      o.fee_bump = false;
      o.bump_fee = 0;
      o.auto_restore = true;
      return o;
    }
  };

  /// Set the decoded contract spec — enables spec-driven JSON arg conversion
  /// via `call_json`/`invoke_json`. `parse_contract_meta` + `ContractSpec::decode`.
  void set_spec(ContractSpec spec) { spec_ = std::move(spec); }
  [[nodiscard]] const ContractSpec* spec() const { return spec_ ? &*spec_ : nullptr; }

  /// Spec-driven call: named args (JSON object) or positional (array) map to
  /// SCVals per the spec's declared input typedefs. Requires `set_spec`.
  [[nodiscard]] Result<xdr::SCVal> call_json(const Address& source,
                                             std::string_view function,
                                             const Json& args);
  /// Spec-driven invoke (same conversion, then the full pipeline).
  [[nodiscard]] Result<TransactionResponse>
  invoke_json(const Address& source, std::int64_t sequence_number,
              const Signer& signer, std::string_view function, const Json& args,
              const InvokeOptions& opts = InvokeOptions::defaults());

  /// The same pipeline on a raw HostFunction — needed for deploy ops
  /// (`upload_wasm_fn`, `create_contract_v2_fn`) and anything not covered by
  /// `invoke()`.
  [[nodiscard]] Result<TransactionResponse>
  invoke_host_function(const Address& source, std::int64_t sequence_number,
                       const Signer& signer, const xdr::HostFunction& fn,
                       const InvokeOptions& opts = InvokeOptions::defaults());

  /// Full invoke: simulate → assemble (auth + resources + optional restore)
  /// → sign → send+poll (optionally fee-bumped). Returns the final status.
  [[nodiscard]] Result<TransactionResponse>
  invoke(const Address& source, std::int64_t sequence_number,
         const Signer& signer, std::string_view function,
         const std::vector<xdr::SCVal>& args,
         const InvokeOptions& opts = InvokeOptions::defaults());

  /// The signature preimage for an auth entry — exposed so signers external
  /// to `invoke` can produce signatures for address credentials.
  [[nodiscard]] Result<Bytes>
  auth_signature_payload(const xdr::SorobanAuthorizationEntry& entry);

  /// Flatten the assembled authorization into a delegate tree view: one row
  /// per credential arm, children = delegates. Read-only.
  [[nodiscard]] Result<std::vector<DelegateNode>>
  invocation_tree(const std::vector<xdr::SorobanAuthorizationEntry>& auth) const;

 private:
  RpcClient& rpc_;
  const Network& network_;
  std::string id_;
  std::optional<ContractSpec> spec_;
};

}  // namespace stellar::inline v1
