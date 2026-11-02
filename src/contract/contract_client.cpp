// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/contract/contract_client.hpp"

#include "stellar/contract/scval.hpp"
#include "stellar/tx/ops.hpp"
#include "stellar/xdr/codec.hpp"

namespace stellar::inline v1 {

Result<std::array<std::uint8_t, 32>>
contract::id_from_address(const Network& network, const Address& deployer,
                          std::array<std::uint8_t, 32> salt) {
  STELLAR_TRY_ASSIGN(auto scaddr, deployer.to_sc_address());
  xdr::HashIDPreimageContractId pc;
  pc.networkID = xdr::uint256{network.id()};
  pc.contractIDPreimage = xdr::ContractIDPreimage::contractIdPreimageFromAddress(
      xdr::ContractIDPreimageFromAddress{scaddr, xdr::uint256{salt}});
  xdr::HashIDPreimage pre = xdr::HashIDPreimage::envelopeTypeContractId(pc);
  STELLAR_TRY_ASSIGN(Bytes bytes, xdr::encode(pre));
  return sha256(ByteSpan(bytes.data(), bytes.size()));
}

std::string contract::contract_strkey(std::array<std::uint8_t, 32> id) {
  StrKey sk{StrKeyKind::kContract, id};
  return sk.encode();
}

Result<xdr::HostFunction>
contract::invoke_fn(std::string_view contract_id, std::string_view function,
                    const std::vector<xdr::SCVal>& args) {
  auto cid = StrKey::decode(contract_id);
  if (!cid.ok() || cid->kind() != StrKeyKind::kContract) {
    return Error::invalid_argument("expected a C... contract id");
  }
  xdr::InvokeContractArgs a;
  a.contractAddress = xdr::SCAddress::scAddressTypeContract(xdr::ContractID{*cid->fixed()});
  a.functionName = xdr::SCSymbol{};
  a.functionName.assign(function.data(), function.size());
  for (const auto& v : args) a.args.push_back(v);
  return xdr::HostFunction::hostFunctionTypeInvokeContract(a);
}

Result<xdr::HostFunction>
contract::create_contract_fn(const Address& deployer, std::array<std::uint8_t, 32> salt,
                             const xdr::ContractExecutable& executable,
                             const std::vector<xdr::SCVal>&) {
  STELLAR_TRY_ASSIGN(auto scaddr, deployer.to_sc_address());
  xdr::CreateContractArgs a;
  a.contractIDPreimage = xdr::ContractIDPreimage::contractIdPreimageFromAddress(
      xdr::ContractIDPreimageFromAddress{scaddr, xdr::uint256{salt}});
  a.executable = executable;
  return xdr::HostFunction::hostFunctionTypeCreateContract(a);
}

Result<xdr::HostFunction>
contract::create_contract_v2_fn(const Address& deployer, std::array<std::uint8_t, 32> salt,
                                const xdr::ContractExecutable& executable,
                                const std::vector<xdr::SCVal>& constructor_args) {
  STELLAR_TRY_ASSIGN(auto scaddr, deployer.to_sc_address());
  xdr::CreateContractArgsV2 a;
  a.contractIDPreimage = xdr::ContractIDPreimage::contractIdPreimageFromAddress(
      xdr::ContractIDPreimageFromAddress{scaddr, xdr::uint256{salt}});
  a.executable = executable;
  for (const auto& v : constructor_args) a.constructorArgs.push_back(v);
  return xdr::HostFunction::hostFunctionTypeCreateContractV2(a);
}

xdr::HostFunction contract::upload_wasm_fn(ByteSpan wasm) {
  xdr::BoundedBytes<xdr::kUnbounded> w;
  w.assign(wasm.begin(), wasm.end());
  return xdr::HostFunction::hostFunctionTypeUploadContractWasm(w);
}

xdr::ContractExecutable contract::wasm_executable(std::array<std::uint8_t, 32> wasm_hash) {
  return xdr::ContractExecutable::contractExecutableWasm(xdr::Hash{wasm_hash});
}
xdr::ContractExecutable contract::sac_executable() {
  return xdr::ContractExecutable::contractExecutableStellarAsset();
}

namespace {
xdr::SCVal make_signature_scval(const std::array<std::uint8_t, 32>& pk,
                                const std::array<std::uint8_t, 64>& sig) {
  return scval::vec({
      scval::map({
          xdr::SCMapEntry{xdr::Box<xdr::SCVal>(scval::symbol("public_key")),
                          xdr::Box<xdr::SCVal>(scval::bytes(ByteSpan(pk.data(), 32)))},
          xdr::SCMapEntry{xdr::Box<xdr::SCVal>(scval::symbol("signature")),
                          xdr::Box<xdr::SCVal>(scval::bytes(ByteSpan(sig.data(), sig.size())))},
      }),
  });
}

/// Shared V1/V2 preimage signing: hash = sha256(ENVELOPE_TYPE_SOROBAN_AUTHORIZATION
/// {networkID, nonce, signatureExpirationLedger, invocation}).
Result<xdr::SCVal> sign_soroban_preimage(const Network& network, const KeyPair& kp,
                                         const xdr::SorobanAuthorizedInvocation& invocation,
                                         std::int64_t nonce, std::uint32_t expiration) {
  xdr::HashIDPreimageSorobanAuthorization pre;
  pre.networkID = xdr::uint256{network.id()};
  pre.invocation = invocation;
  pre.nonce = static_cast<xdr::int64>(nonce);
  pre.signatureExpirationLedger = expiration;
  xdr::HashIDPreimage hp = xdr::HashIDPreimage::envelopeTypeSorobanAuthorization(pre);
  STELLAR_TRY_ASSIGN(Bytes bytes, xdr::encode(hp));
  const auto hash = sha256(ByteSpan(bytes.data(), bytes.size()));
  STELLAR_TRY_ASSIGN(auto sig, kp.sign(ByteSpan(hash.data(), hash.size())));
  return make_signature_scval(kp.public_key(), sig);
}
}  // namespace

Result<xdr::SorobanAuthorizationEntry>
contract::sign_auth_entry(const xdr::SorobanAuthorizationEntry& entry,
                          const Network& network, const KeyPair& kp) {
  switch (entry.credentials.type()) {
    case xdr::SorobanCredentialsType::SOROBAN_CREDENTIALS_SOURCE_ACCOUNT:
      return entry;  // nothing to sign — tx-level sig covers it
    case xdr::SorobanCredentialsType::SOROBAN_CREDENTIALS_ADDRESS: {
      const auto& addr_creds = entry.credentials.address();
      STELLAR_TRY_ASSIGN(auto sig_val,
                         sign_soroban_preimage(network, kp, entry.rootInvocation,
                                               addr_creds.nonce,
                                               addr_creds.signatureExpirationLedger));
      xdr::SorobanAuthorizationEntry out = entry;
      out.credentials = xdr::SorobanCredentials::sorobanCredentialsAddress(
          xdr::SorobanAddressCredentials{addr_creds.address, addr_creds.nonce,
                                         addr_creds.signatureExpirationLedger, sig_val});
      return out;
    }
    case xdr::SorobanCredentialsType::SOROBAN_CREDENTIALS_ADDRESS_V2: {
      const auto& ac = entry.credentials.addressV2();
      STELLAR_TRY_ASSIGN(auto sig_val,
                         sign_soroban_preimage(network, kp, entry.rootInvocation,
                                               ac.nonce, ac.signatureExpirationLedger));
      xdr::SorobanAuthorizationEntry out = entry;
      auto v2 = ac;
      v2.signature = sig_val;
      out.credentials = xdr::SorobanCredentials::sorobanCredentialsAddressV2(v2);
      return out;
    }
    case xdr::SorobanCredentialsType::SOROBAN_CREDENTIALS_ADDRESS_WITH_DELEGATES: {
      // root credential gets the same preimage signature; the delegates
      // themselves are signed separately by their own addresses.
      const auto& wd = entry.credentials.addressWithDelegates();
      const auto& ac = wd.addressCredentials;
      STELLAR_TRY_ASSIGN(auto sig_val,
                         sign_soroban_preimage(network, kp, entry.rootInvocation,
                                               ac.nonce, ac.signatureExpirationLedger));
      xdr::SorobanAuthorizationEntry out = entry;
      auto wd_out = wd;
      wd_out.addressCredentials.signature = sig_val;
      out.credentials =
          xdr::SorobanCredentials::sorobanCredentialsAddressWithDelegates(wd_out);
      return out;
    }
    default:
      return Error{ErrorCode::kNotSupported, "unsupported credentials type"};
  }
}

// ---------------------------------------------------------------------------

Result<xdr::SCVal> ContractClient::call(const Address& source, std::string_view function,
                                        const std::vector<xdr::SCVal>& args) {
  STELLAR_TRY_ASSIGN(auto fn, contract::invoke_fn(id_, function, args));
  TransactionBuilder b(source);
  b.sequence_number(0).fee(100).no_timeout().add_operation(ops::invoke_host_function(fn));
  STELLAR_TRY_ASSIGN(auto tx, b.build());
  STELLAR_TRY_ASSIGN(auto sim, rpc_.simulate_transaction(tx));
  if (sim.error) return Error{ErrorCode::kRpcError, *sim.error};
  // the host-function return value rides in results[].xdr as a bare SCVal
  if (sim.results.empty()) {
    return Error{ErrorCode::kProtocolError, "simulation returned no result value"};
  }
  if (sim.result &&
      sim.result->result.code() == xdr::TransactionResultCode::txSUCCESS) {
    // sanity-check the op result too when present
    const auto& ops_res = sim.result->result.results();
    if (!ops_res.empty() &&
        ops_res[0].code() == xdr::OperationResultCode::opINNER &&
        ops_res[0].tr().type() == xdr::OperationType::INVOKE_HOST_FUNCTION &&
        ops_res[0].tr().invokeHostFunctionResult().code() !=
            xdr::InvokeHostFunctionResultCode::INVOKE_HOST_FUNCTION_SUCCESS) {
      return Error{ErrorCode::kTransactionFailed, "simulated call failed"};
    }
  }
  return sim.results[0];
}

Result<TransactionResponse> ContractClient::invoke(
    const Address& source, std::int64_t sequence_number, const Signer& signer,
    std::string_view function, const std::vector<xdr::SCVal>& args,
    const InvokeOptions& opts) {
  STELLAR_TRY_ASSIGN(auto fn, contract::invoke_fn(id_, function, args));
  return invoke_host_function(source, sequence_number, signer, fn, opts);
}

Result<TransactionResponse> ContractClient::invoke_host_function(
    const Address& source, std::int64_t sequence_number, const Signer& signer,
    const xdr::HostFunction& fn, const InvokeOptions& opts) {
  TransactionBuilder b(source);
  b.sequence_number(sequence_number)
      .fee(opts.fee ? opts.fee : 100)
      .no_timeout()
      .add_operation(ops::invoke_host_function(fn));
  STELLAR_TRY_ASSIGN(auto tx, b.build());
  STELLAR_TRY_ASSIGN(auto sim, rpc_.simulate_transaction(tx));
  if (sim.error) return Error{ErrorCode::kRpcError, *sim.error};
  if (!sim.transaction_data) {
    return Error{ErrorCode::kProtocolError, "simulation returned no soroban data"};
  }
  // auto-restore: prepend restore op if sim flagged expired state
  if (opts.auto_restore && sim.restore_preamble) {
    // sim flagged expired state: prepend a restore op to cover it
    b.add_operation(ops::restore_footprint(&source));
  }
  // assemble: attach soroban data + auth to the tx, then sign+send
  tx.ext = xdr::TransactionExt::v1(xdr::SorobanTransactionData{});
  *tx.ext.get_if_sorobanData() = *sim.transaction_data;
  auto& op = tx.operations[0];
  op.body.invokeHostFunctionOp().auth.clear();
  for (auto& a : sim.auth) {
    // sign any address-credential entries; source-account passes through
    if (auto* kp = dynamic_cast<const KeyPairSigner*>(&signer)) {
      auto signed_entry = contract::sign_auth_entry(a, network_, kp->keypair());
      if (signed_entry.ok()) a = std::move(*signed_entry);
    }
    op.body.invokeHostFunctionOp().auth.push_back(a);
  }
  // sim's resourceFee is an estimate; pad by the configured leeway so price
  // drift between sim and submission doesn't underpay.
  auto& sd = tx.ext.sorobanData();
  sd.resourceFee += (sd.resourceFee * opts.resource_leeway_pct) / 100;
  tx.fee = static_cast<xdr::int64>(opts.fee ? opts.fee : 100) + sd.resourceFee;
  auto env = TransactionEnvelope::of_transaction(tx);
  STELLAR_TRY(env.sign(signer, network_));
  if (!opts.fee_bump) return rpc_.send_and_poll(env);
  // wrap in fee-bump
  FeeBumpBuilder fb(source);
  fb.fee(opts.bump_fee ? opts.bump_fee
                       : static_cast<std::int64_t>(opts.fee ? opts.fee : 100) +
                             sd.resourceFee + 100);
  STELLAR_TRY_ASSIGN(auto bumped, fb.build(env));
  auto bumped_env = TransactionEnvelope::of_fee_bump(bumped);
  STELLAR_TRY(bumped_env.sign(signer, network_));
  return rpc_.send_and_poll(bumped_env);
}

Result<Bytes> ContractClient::auth_signature_payload(
    const xdr::SorobanAuthorizationEntry& entry) {
  // expose the exact preimage the signer signs — external signers use this
  const auto* cred = entry.credentials.get_if_address();
  if (!cred) {
    return Error::invalid_argument("entry is not address-credentials");
  }
  xdr::HashIDPreimageSorobanAuthorization pre;
  pre.networkID = xdr::uint256{network_.id()};
  pre.invocation = entry.rootInvocation;
  pre.nonce = cred->nonce;
  pre.signatureExpirationLedger = cred->signatureExpirationLedger;
  return xdr::encode(
      xdr::HashIDPreimage::envelopeTypeSorobanAuthorization(pre));
}

Result<std::vector<DelegateNode>> ContractClient::invocation_tree(
    const std::vector<xdr::SorobanAuthorizationEntry>& auth) const {
  std::vector<DelegateNode> roots;
  for (const auto& e : auth) {
    DelegateNode n;
    if (const auto* cred = e.credentials.get_if_address()) {
      n.address = cred->address;
      n.signature = cred->signature;
    } else if (const auto* cred = e.credentials.get_if_addressV2()) {
      n.address = cred->address;
      n.signature = cred->signature;
    } else if (const auto* cred = e.credentials.get_if_addressWithDelegates()) {
      n.address = cred->addressCredentials.address;
      n.signature = cred->addressCredentials.signature;
      std::function<void(const xdr::SorobanDelegateSignature&, DelegateNode&)>
          fill = [&fill](const xdr::SorobanDelegateSignature& d,
                         DelegateNode& node) {
            node.address = d.address;
            node.signature = *d.signature;
            if (!d.nestedDelegates->empty()) {
              for (const auto& c : *d.nestedDelegates) {
                DelegateNode child;
                fill(c, child);
                node.children.push_back(std::move(child));
              }
            }
          };
      for (const auto& d : cred->delegates) {
        DelegateNode child;
        fill(d, child);
        n.children.push_back(std::move(child));
      }
    } else {
      continue;  // source-account creds carry no signature
    }
    roots.push_back(std::move(n));
  }
  return roots;
}

Result<xdr::SCVal> ContractClient::call_json(const Address& source,
                                               std::string_view function,
                                               const Json& args) {
  if (!spec_) {
    return Error::invalid_argument("call_json requires set_spec");
  }
  STELLAR_TRY_ASSIGN(auto scvals, spec_->args_from_json(function, args));
  return call(source, function, scvals);
}

Result<TransactionResponse> ContractClient::invoke_json(
    const Address& source, std::int64_t sequence_number, const Signer& signer,
    std::string_view function, const Json& args, const InvokeOptions& opts) {
  if (!spec_) {
    return Error::invalid_argument("invoke_json requires set_spec");
  }
  STELLAR_TRY_ASSIGN(auto scvals, spec_->args_from_json(function, args));
  return invoke(source, sequence_number, signer, function, scvals, opts);
}

}  // namespace stellar::inline v1
