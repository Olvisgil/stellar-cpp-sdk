// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file ops.hpp
/// Fluent factories for every OperationBody arm, converting domain types
/// (Address/Asset/Amount/Price) into the generated XDR structs.

#include "stellar/core/address.hpp"
#include "stellar/core/amount.hpp"
#include "stellar/core/asset.hpp"
#include "stellar/core/price.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace stellar::inline v1::ops {

namespace detail {

inline Result<xdr::MuxedAccount> to_muxed(const Address& a) { return a.to_muxed_account(); }

inline Result<xdr::AccountID> to_account(const Address& a) {
  const auto* pk = a.account_id_bytes();
  if (!pk) return Error::invalid_argument("expected an account address");
  return xdr::PublicKey::publicKeyTypeEd25519(xdr::uint256{*pk});
}

inline void set_source(xdr::Operation& op, const Address* source) {
  if (source) {
    if (auto m = source->to_muxed_account(); m.ok()) op.sourceAccount = *m;
  }
}

inline xdr::Operation make(xdr::OperationBody body, const Address* source) {
  xdr::Operation op;
  op.body = std::move(body);
  set_source(op, source);
  return op;
}

inline Result<xdr::ChangeTrustAsset> to_change_trust(const Asset& a) {
  return a.to_change_trust_asset();
}

inline Result<xdr::AssetCode> to_asset_code(const Asset& a) {
  if (a.kind() == Asset::Kind::kAlphaNum4) {
    xdr::AssetCode4 c{};
    std::memcpy(c.data(), a.code().data(), a.code().size());
    return xdr::AssetCode::assetTypeCreditAlphanum4(c);
  }
  if (a.kind() == Asset::Kind::kAlphaNum12) {
    xdr::AssetCode12 c{};
    std::memcpy(c.data(), a.code().data(), a.code().size());
    return xdr::AssetCode::assetTypeCreditAlphanum12(c);
  }
  return Error::invalid_argument("native asset has no AssetCode");
}

}  // namespace detail

// ---------------------------------------------------------------------------
// account / payment
// ---------------------------------------------------------------------------

inline Result<xdr::Operation> create_account(const Address& destination,
                                             Amount starting_balance,
                                             const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto dest, detail::to_account(destination));
  if (!starting_balance.positive() && starting_balance.stroops() != 0) {
    return Error::invalid_argument("starting balance must be >= 0");
  }
  return detail::make(xdr::OperationBody::createAccount(
                          xdr::CreateAccountOp{dest, starting_balance.stroops()}),
                      source);
}

inline Result<xdr::Operation> payment(const Address& destination, const Asset& asset,
                                      Amount amount, const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto dest, detail::to_muxed(destination));
  STELLAR_TRY_ASSIGN(auto xa, asset.to_xdr());
  return detail::make(
      xdr::OperationBody::payment(xdr::PaymentOp{dest, xa, amount.stroops()}), source);
}

inline Result<xdr::Operation> account_merge(const Address& destination,
                                            const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto dest, detail::to_muxed(destination));
  return detail::make(xdr::OperationBody::accountMerge(dest), source);
}

[[deprecated("removed by protocol 12; kept for historical envelopes only")]]
inline xdr::Operation inflation(const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::inflation(), source);
}

// ---------------------------------------------------------------------------
// path payments
// ---------------------------------------------------------------------------

inline Result<xdr::Operation> path_payment_strict_receive(
    const Asset& send_asset, Amount send_max, const Address& destination,
    const Asset& dest_asset, Amount dest_amount, const std::vector<Asset>& path = {},
    const Address* source = nullptr) {
  if (path.size() > 5) return Error::invalid_argument("path may have at most 5 hops");
  STELLAR_TRY_ASSIGN(auto dest, detail::to_muxed(destination));
  STELLAR_TRY_ASSIGN(auto xs, send_asset.to_xdr());
  STELLAR_TRY_ASSIGN(auto xd, dest_asset.to_xdr());
  xdr::BoundedVector<xdr::Asset, 5> xp;
  for (const auto& a : path) {
    STELLAR_TRY_ASSIGN(auto x, a.to_xdr());
    xp.push_back(x);
  }
  return detail::make(xdr::OperationBody::pathPaymentStrictReceive(
                          xdr::PathPaymentStrictReceiveOp{xs, send_max.stroops(), dest, xd,
                                                        dest_amount.stroops(), xp}),
                      source);
}

inline Result<xdr::Operation> path_payment_strict_send(
    const Asset& send_asset, Amount send_amount, const Address& destination,
    const Asset& dest_asset, Amount dest_min, const std::vector<Asset>& path = {},
    const Address* source = nullptr) {
  if (path.size() > 5) return Error::invalid_argument("path may have at most 5 hops");
  STELLAR_TRY_ASSIGN(auto dest, detail::to_muxed(destination));
  STELLAR_TRY_ASSIGN(auto xs, send_asset.to_xdr());
  STELLAR_TRY_ASSIGN(auto xd, dest_asset.to_xdr());
  xdr::BoundedVector<xdr::Asset, 5> xp;
  for (const auto& a : path) {
    STELLAR_TRY_ASSIGN(auto x, a.to_xdr());
    xp.push_back(x);
  }
  return detail::make(xdr::OperationBody::pathPaymentStrictSend(
                          xdr::PathPaymentStrictSendOp{xs, send_amount.stroops(), dest, xd,
                                                       dest_min.stroops(), xp}),
                      source);
}

// ---------------------------------------------------------------------------
// offers / orderbook
// ---------------------------------------------------------------------------

inline Result<xdr::Operation> manage_sell_offer(const Asset& selling, const Asset& buying,
                                                Amount amount, Price price, std::int64_t offer_id = 0,
                                                const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto xs, selling.to_xdr());
  STELLAR_TRY_ASSIGN(auto xb, buying.to_xdr());
  return detail::make(xdr::OperationBody::manageSellOffer(
                          xdr::ManageSellOfferOp{xs, xb, amount.stroops(), price.to_xdr(),
                                                 static_cast<xdr::int64>(offer_id)}),
                      source);
}

inline Result<xdr::Operation> manage_buy_offer(const Asset& selling, const Asset& buying,
                                               Amount buy_amount, Price price,
                                               std::int64_t offer_id = 0,
                                               const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto xs, selling.to_xdr());
  STELLAR_TRY_ASSIGN(auto xb, buying.to_xdr());
  return detail::make(xdr::OperationBody::manageBuyOffer(
                          xdr::ManageBuyOfferOp{xs, xb, buy_amount.stroops(), price.to_xdr(),
                                                static_cast<xdr::int64>(offer_id)}),
                      source);
}

inline Result<xdr::Operation> create_passive_sell_offer(const Asset& selling,
                                                        const Asset& buying, Amount amount,
                                                        Price price,
                                                        const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto xs, selling.to_xdr());
  STELLAR_TRY_ASSIGN(auto xb, buying.to_xdr());
  return detail::make(xdr::OperationBody::createPassiveSellOffer(
                          xdr::CreatePassiveSellOfferOp{xs, xb, amount.stroops(),
                                                      price.to_xdr()}),
                      source);
}

// ---------------------------------------------------------------------------
// trustlines
// ---------------------------------------------------------------------------

inline Result<xdr::Operation> change_trust(const Asset& asset, Amount limit,
                                           const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto line, detail::to_change_trust(asset));
  return detail::make(
      xdr::OperationBody::changeTrust(xdr::ChangeTrustOp{line, limit.stroops()}), source);
}

/// Change trust with a raw ChangeTrustAsset (supports pool shares).
inline Result<xdr::Operation> change_trust_asset(const xdr::ChangeTrustAsset& line,
                                                 Amount limit,
                                                 const Address* source = nullptr) {
  return detail::make(
      xdr::OperationBody::changeTrust(xdr::ChangeTrustOp{line, limit.stroops()}), source);
}

[[deprecated("use set_trust_line_flags instead")]]
inline Result<xdr::Operation> allow_trust(const Address& trustor, const Asset& asset,
                                          std::uint32_t authorize_flags,
                                          const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto acc, detail::to_account(trustor));
  STELLAR_TRY_ASSIGN(auto code, detail::to_asset_code(asset));
  return detail::make(xdr::OperationBody::allowTrust(
                          xdr::AllowTrustOp{acc, code, authorize_flags}),
                      source);
}

inline Result<xdr::Operation> set_trust_line_flags(const Address& trustor, const Asset& asset,
                                                     std::uint32_t clear_flags,
                                                     std::uint32_t set_flags,
                                                     const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto acc, detail::to_account(trustor));
  STELLAR_TRY_ASSIGN(auto xa, asset.to_xdr());
  return detail::make(xdr::OperationBody::setTrustLineFlags(
                          xdr::SetTrustLineFlagsOp{acc, xa, clear_flags, set_flags}),
                      source);
}

// ---------------------------------------------------------------------------
// account options
// ---------------------------------------------------------------------------

/// Fluent builder for SET_OPTIONS (everything optional).
class STELLAR_API SetOptionsBuilder {
 public:
  SetOptionsBuilder& inflation_destination(const Address& a);
  SetOptionsBuilder& clear_flags(std::uint32_t f) { op_.clearFlags = f; return *this; }
  SetOptionsBuilder& set_flags(std::uint32_t f) { op_.setFlags = f; return *this; }
  SetOptionsBuilder& master_weight(std::uint32_t w) { op_.masterWeight = w; return *this; }
  SetOptionsBuilder& low_threshold(std::uint32_t t) { op_.lowThreshold = t; return *this; }
  SetOptionsBuilder& med_threshold(std::uint32_t t) { op_.medThreshold = t; return *this; }
  SetOptionsBuilder& high_threshold(std::uint32_t t) { op_.highThreshold = t; return *this; }
  SetOptionsBuilder& home_domain(std::string_view d);
  SetOptionsBuilder& signer(xdr::SignerKey key, std::uint32_t weight);

  [[nodiscard]] xdr::Operation build(const Address* source = nullptr) const {
    return detail::make(xdr::OperationBody::setOptions(op_), source);
  }

 private:
  xdr::SetOptionsOp op_{};
};
inline SetOptionsBuilder set_options() { return {}; }

// ---------------------------------------------------------------------------
// data / sequence
// ---------------------------------------------------------------------------

inline Result<xdr::Operation> manage_data(std::string_view name,
                                          std::optional<Bytes> value,
                                          const Address* source = nullptr) {
  if (name.size() > 64) return Error::invalid_argument("data name > 64 bytes");
  xdr::BoundedString<64> n;
  n.assign(name.data(), name.size());
  xdr::ManageDataOp op{n, std::nullopt};
  if (value) {
    if (value->size() > 64) return Error::invalid_argument("data value > 64 bytes");
    op.dataValue = xdr::DataValue(value->begin(), value->end());
  }
  return detail::make(xdr::OperationBody::manageData(op), source);
}

inline xdr::Operation bump_sequence(std::int64_t bump_to, const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::bumpSequence(
                          xdr::BumpSequenceOp{static_cast<xdr::int64>(bump_to)}),
                      source);
}

// ---------------------------------------------------------------------------
// claimable balances
// ---------------------------------------------------------------------------

/// Helper to build a CLAIMANT_V0 claimant with an unconditional predicate.
inline Result<xdr::Claimant> claimant_v0(const Address& destination,
                                         const xdr::ClaimPredicate& predicate) {
  STELLAR_TRY_ASSIGN(auto acc, detail::to_account(destination));
  return xdr::Claimant::claimantTypeV0(xdr::ClaimantV0{acc, predicate});
}

inline Result<xdr::Operation> create_claimable_balance(
    const Asset& asset, Amount amount, const std::vector<xdr::Claimant>& claimants,
    const Address* source = nullptr) {
  if (claimants.empty() || claimants.size() > 10) {
    return Error::invalid_argument("claimable balance needs 1..10 claimants");
  }
  STELLAR_TRY_ASSIGN(auto xa, asset.to_xdr());
  xdr::BoundedVector<xdr::Claimant, 10> xc;
  for (const auto& c : claimants) xc.push_back(c);
  return detail::make(xdr::OperationBody::createClaimableBalance(
                          xdr::CreateClaimableBalanceOp{xa, amount.stroops(), xc}),
                      source);
}

inline xdr::Operation claim_claimable_balance(xdr::ClaimableBalanceID id,
                                              const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::claimClaimableBalance(
                          xdr::ClaimClaimableBalanceOp{id}),
                      source);
}

// ---------------------------------------------------------------------------
// sponsorship
// ---------------------------------------------------------------------------

inline Result<xdr::Operation> begin_sponsoring_future_reserves(const Address& sponsored,
                                                             const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto acc, detail::to_account(sponsored));
  return detail::make(xdr::OperationBody::beginSponsoringFutureReserves(
                          xdr::BeginSponsoringFutureReservesOp{acc}),
                      source);
}

inline xdr::Operation end_sponsoring_future_reserves(const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::endSponsoringFutureReserves(), source);
}

inline xdr::Operation revoke_sponsorship_ledger_entry(xdr::LedgerKey key,
                                                      const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::revokeSponsorship(
                          xdr::RevokeSponsorshipOp::revokeSponsorshipLedgerEntry(key)),
                      source);
}

inline Result<xdr::Operation> revoke_sponsorship_signer(const Address& account,
                                                      xdr::SignerKey key,
                                                      const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto acc, detail::to_account(account));
  return detail::make(xdr::OperationBody::revokeSponsorship(
                          xdr::RevokeSponsorshipOp::revokeSponsorshipSigner(
                              xdr::RevokeSponsorshipOpSigner{acc, key})),
                      source);
}

// ---------------------------------------------------------------------------
// clawback
// ---------------------------------------------------------------------------

inline Result<xdr::Operation> clawback(const Asset& asset, const Address& from,
                                       Amount amount, const Address* source = nullptr) {
  STELLAR_TRY_ASSIGN(auto xa, asset.to_xdr());
  STELLAR_TRY_ASSIGN(auto from_m, detail::to_muxed(from));
  return detail::make(
      xdr::OperationBody::clawback(xdr::ClawbackOp{xa, from_m, amount.stroops()}), source);
}

inline xdr::Operation clawback_claimable_balance(xdr::ClaimableBalanceID id,
                                                 const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::clawbackClaimableBalance(
                          xdr::ClawbackClaimableBalanceOp{id}),
                      source);
}

// ---------------------------------------------------------------------------
// liquidity pools
// ---------------------------------------------------------------------------

inline xdr::Operation liquidity_pool_deposit(std::array<std::uint8_t, 32> pool_id,
                                             Amount max_a, Amount max_b, Price min_price,
                                             Price max_price, const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::liquidityPoolDeposit(
                          xdr::LiquidityPoolDepositOp{xdr::PoolID{pool_id},
                                                      max_a.stroops(), max_b.stroops(),
                                                      min_price.to_xdr(), max_price.to_xdr()}),
                      source);
}

inline xdr::Operation liquidity_pool_withdraw(std::array<std::uint8_t, 32> pool_id,
                                              Amount pool_amount, Amount min_a, Amount min_b,
                                              const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::liquidityPoolWithdraw(
                          xdr::LiquidityPoolWithdrawOp{xdr::PoolID{pool_id},
                                                       pool_amount.stroops(), min_a.stroops(),
                                                       min_b.stroops()}),
                      source);
}

// ---------------------------------------------------------------------------
// Soroban
// ---------------------------------------------------------------------------

inline xdr::Operation invoke_host_function(
    xdr::HostFunction fn, const std::vector<xdr::SorobanAuthorizationEntry>& auth = {},
    const Address* source = nullptr) {
  xdr::InvokeHostFunctionOp op{fn};
  for (const auto& a : auth) op.auth.push_back(a);
  return detail::make(xdr::OperationBody::invokeHostFunction(op), source);
}

inline xdr::Operation extend_footprint_ttl(std::uint32_t extend_to,
                                           const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::extendFootprintTtl(
                          xdr::ExtendFootprintTTLOp{xdr::ExtensionPoint::v0(),
                                                  static_cast<xdr::uint32>(extend_to)}),
                      source);
}

inline xdr::Operation restore_footprint(const Address* source = nullptr) {
  return detail::make(xdr::OperationBody::restoreFootprint(xdr::RestoreFootprintOp{
                          xdr::ExtensionPoint::v0()}),
                      source);
}

}  // namespace stellar::inline v1::ops
