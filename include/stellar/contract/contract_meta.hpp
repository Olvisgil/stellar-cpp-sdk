// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
/// @file contract_meta.hpp
/// Read-only WASM metadata: custom-section parser (no execution) extracting
/// SEP-46/47/48 key-value entries and the ScSpecEntry stream.
#include <map>
#include <string>
#include <vector>
#include "stellar/contract/scval.hpp"
#include "stellar/core/address.hpp"
#include "stellar/core/bigint.hpp"
#include "stellar/export.hpp"
#include "stellar/json.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"
#include "stellar/util/encoding.hpp"
#include "stellar/xdr/generated/stellar_contract.hpp"
#include "stellar/xdr/generated/stellar_contract_spec.hpp"
#include "stellar/xdr/generated/stellar_ledger.hpp"
namespace stellar::inline v1 {

/// WASM metadata extracted from custom sections — read-only, no execution.
struct STELLAR_API ContractMeta {
  std::string name;
  std::string version;
  std::vector<std::string> interfaces;     // interface names
  std::vector<std::string> env_meta;       // env_meta entries
  std::map<std::string, std::string> kvs;  // arbitrary meta key/values
  Bytes spec_entries_xdr;                  // raw ScSpecEntry XDR stream
};

/// Parse a WASM binary for the soroban meta custom sections:
///   "contractenvmetav0"    — env meta (XDR string vec)
///   "contractspecv0"       — ScSpecEntry stream (XDR vec)
///   "contractmetav0"       — SEP-46-style key/value pairs (XDR map)
/// Errors only on malformed WASM/section structure; absent sections yield
/// an empty ContractMeta.
[[nodiscard]] Result<ContractMeta> parse_contract_meta(ByteSpan wasm);

/// Spec-driven argument conversion: turn a human-supplied string + a spec
/// type name (from ScSpecEntry function inputs) into an SCVal.
/// Supported: bool/u32/i32/u64/i64/u128/i128/u256/i256/string/symbol/
///   address/bytes/bytesN — unknown arms error out.
[[nodiscard]] Result<xdr::SCVal> scval_from_string(std::string_view spec_type,
                                                   std::string_view arg);

/// Decoded contract spec: function signatures + UDT registry.
class STELLAR_API ContractSpec {
 public:
  /// Decode a `contractspecv0` stream (concatenated ScSpecEntry XDR).
  [[nodiscard]] static Result<ContractSpec> decode(ByteSpan spec_xdr);

  /// Look up a function by name; nullptr if absent.
  [[nodiscard]] const xdr::SCSpecFunctionV0* function(std::string_view name) const;

  /// Convert named args (JSON object keyed by input name) or positional
  /// (JSON array) into SCVal args per the function's declared inputs.
  /// UDTs resolve against the spec's own struct/union/enum registry.
  [[nodiscard]] Result<std::vector<xdr::SCVal>> args_from_json(std::string_view function_name,
                                                               const Json& args) const;

  /// Convert a single arg by typedef — public so callers can do
  /// return-value decode with the same machinery.
  [[nodiscard]] Result<xdr::SCVal> arg_to_scval(const xdr::SCSpecTypeDef& type,
                                                const Json& arg) const;

  /// The decoded entries (for inspection / event spec lookup).
  [[nodiscard]] const std::vector<xdr::SCSpecEntry>& entries() const { return entries_; }

 private:
  std::vector<xdr::SCSpecEntry> entries_;
  std::map<std::string, xdr::SCSpecFunctionV0> functions_;
  std::map<std::string, const xdr::SCSpecEntry*> udts_;  // name -> struct/union/enum
};

}  // namespace stellar::inline v1
