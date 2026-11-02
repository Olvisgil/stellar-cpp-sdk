// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/contract/token_client.hpp"
#include "stellar/xdr/codec.hpp"

#include <sstream>

namespace stellar::inline v1 {

namespace {

// i128 amounts arrive as Int128Parts{hi, lo}; we surface them as int64
// (covers every realistic token amount).
Result<std::int64_t> i128_result(const xdr::SCVal& v) {
  if (v.type() != xdr::SCValType::SCV_I128) {
    return Error::invalid_argument("result is not i128");
  }
  const auto& p = v.i128();
  if (p.hi != 0 && p.hi != -1) {
    return Error::invalid_argument("i128 result does not fit int64");
  }
  return static_cast<std::int64_t>(p.lo);
}

Result<std::int64_t> read_i128(ContractClient& cc, const Address& src,
                               std::string_view fn,
                               const std::vector<xdr::SCVal>& args) {
  STELLAR_TRY_ASSIGN(auto v, cc.call(src, fn, args));
  return i128_result(v);
}

Result<std::string> read_str(ContractClient& cc, const Address& src,
                             std::string_view fn) {
  STELLAR_TRY_ASSIGN(auto v, cc.call(src, fn, {}));
  if (const auto* s = scval_get::string(v)) {
    return std::string(s->begin(), s->end());
  }
  if (const auto* s = scval_get::symbol(v)) {
    return std::string(s->begin(), s->end());
  }
  return Error::invalid_argument("result is not string/symbol");
}

}  // namespace

Result<std::int64_t> TokenClient::balance(const Address& src, const Address& owner) {
  return read_i128(client_, src, "balance", {scval::address(owner)});
}
Result<std::int64_t> TokenClient::allowance(const Address& src, const Address& from,
                                            const Address& spender) {
  return read_i128(client_, src, "allowance",
                   {scval::address(from), scval::address(spender)});
}
Result<std::int64_t> TokenClient::decimals(const Address& src) {
  STELLAR_TRY_ASSIGN(auto v, client_.call(src, "decimals", {}));
  const auto* x = scval_get::u32(v);
  if (!x) return Error::invalid_argument("result is not u32");
  return static_cast<std::int64_t>(*x);
}
Result<std::string> TokenClient::name(const Address& src) {
  return read_str(client_, src, "name");
}
Result<std::string> TokenClient::symbol(const Address& src) {
  return read_str(client_, src, "symbol");
}
Result<std::int64_t> TokenClient::total_supply(const Address& src) {
  STELLAR_TRY_ASSIGN(auto v, client_.call(src, "total_supply", {}));
  return i128_result(v);
}
Result<std::int64_t> TokenClient::burnable(const Address& src, const Address& who) {
  return read_i128(client_, src, "burnable", {scval::address(who)});
}

Result<TransactionResponse> TokenClient::transfer(
    const Address& source, std::int64_t seq, const Signer& signer,
    const Address& from, const Address& to, std::int64_t amount) {
  return client_.invoke(source, seq, signer, "transfer",
                        {scval::address(from), scval::address(to),
                         scval::i128(0, static_cast<std::uint64_t>(amount))});
}
Result<TransactionResponse> TokenClient::approve(
    const Address& source, std::int64_t seq, const Signer& signer,
    const Address& from, const Address& spender, std::int64_t amount,
    std::uint32_t expiration_ledger) {
  return client_.invoke(source, seq, signer, "approve",
                        {scval::address(from), scval::address(spender),
                         scval::i128(0, static_cast<std::uint64_t>(amount)),
                         scval::u32(expiration_ledger)});
}
Result<TransactionResponse> TokenClient::burn(
    const Address& source, std::int64_t seq, const Signer& signer,
    const Address& from, std::int64_t amount) {
  return client_.invoke(source, seq, signer, "burn",
                        {scval::address(from),
                         scval::i128(0, static_cast<std::uint64_t>(amount))});
}
Result<TransactionResponse> TokenClient::clawback(
    const Address& source, std::int64_t seq, const Signer& signer,
    const Address& admin, const Address& from, std::int64_t amount) {
  return client_.invoke(source, seq, signer, "clawback",
                        {scval::address(admin), scval::address(from),
                         scval::i128(0, static_cast<std::uint64_t>(amount))});
}

Result<Address> TokenClient::event_address(const xdr::SCVal& v) {
  const auto* a = scval_get::address(v);
  if (!a) return Error::invalid_argument("value is not an address");
  return Address::from_sc_address(*a);
}

bool TokenClient::is_muxed_address(const xdr::SCVal& v) {
  const auto* a = scval_get::address(v);
  return a &&
         a->type() == xdr::SCAddressType::SC_ADDRESS_TYPE_MUXED_CONTRACT;
}

Result<std::pair<std::string, std::string>> TokenClient::decode_event(
    const EventRow& event) {
  if (event.topic.empty()) return Error::invalid_argument("no topic");
  std::string topic = "<non-symbol>";
  if (const auto* s = scval_get::symbol(event.topic[0])) {
    topic = std::string(s->begin(), s->end());
  }
  return std::make_pair(topic, scval_to_string(event.value));
}

}  // namespace stellar::inline v1
