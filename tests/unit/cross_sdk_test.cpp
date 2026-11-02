// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
// Cross-SDK byte compatibility: rebuild every transaction in
// tests/fixtures/cross_sdk/fixtures_js.json (generated with stellar-base)
// and require byte-identical signed envelopes + hashes.
#include "stellar/core/asset.hpp"
#include "stellar/json.hpp"
#include "stellar/tx/ops.hpp"
#include "stellar/tx/transaction.hpp"
#include "stellar/xdr/codec.hpp"

#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace stellar::v1;

namespace {

const Network kNet = Network::testnet();
constexpr std::int64_t kSeq = 10000000000LL;  // stellar-base seq + 1

Address src_addr() {
  // derived from 32-byte seed of 0x5a
  return *Address::parse("GAGXKUDVJYEABJOSG7XPLATAGV3GXGZ6LIKYNCUUBKZITFMHRDR3BMEA");
}

KeyPairSigner src_signer() {
  Bytes seed(32, 0x5a);
  auto kp = KeyPair::from_seed(seed);
  return KeyPairSigner(std::move(*kp));
}

Asset usd() {
  return *Asset::parse("USD:GAGXKUDVJYEABJOSG7XPLATAGV3GXGZ6LIKYNCUUBKZITFMHRDR3BMEA");
}
Asset eur() {
  return *Asset::parse("EUR:GAGXKUDVJYEABJOSG7XPLATAGV3GXGZ6LIKYNCUUBKZITFMHRDR3BMEA");
}

TransactionBuilder base() {
  TransactionBuilder b(src_addr());
  b.sequence_number(kSeq).fee(100).time_bounds(1000000, 2000000);
  return b;
}

xdr::ClaimPredicate unconditional() {
  return xdr::ClaimPredicate::claimPredicateUnconditional();
}

Result<xdr::Operation> build_op(const std::string& name, const Address& src) {
  using ops::set_options;
  if (name == "payment_native")
    return ops::payment(src, Asset::native(), *Amount::parse("12.3456789"));
  if (name == "payment_1xlm") return ops::payment(src, Asset::native(), *Amount::parse("1"));
  if (name == "payment_usd") return ops::payment(src, usd(), *Amount::parse("0.0000001"));
  if (name == "create_account") return ops::create_account(src, *Amount::parse("1.5"));
  if (name == "path_payment_strict_receive")
    return ops::path_payment_strict_receive(usd(), *Amount::parse("10"), src, Asset::native(),
                                            *Amount::parse("5"), {eur()});
  if (name == "path_payment_strict_send")
    return ops::path_payment_strict_send(Asset::native(), *Amount::parse("10"), src, usd(),
                                         *Amount::parse("5"), {eur(), usd()});
  if (name == "manage_sell_offer")
    return ops::manage_sell_offer(usd(), Asset::native(), *Amount::parse("7.5"),
                                  *Price::parse("2.5"));
  if (name == "manage_sell_offer_delete")
    return ops::manage_sell_offer(usd(), Asset::native(), *Amount::parse("0"), *Price::parse("2.5"),
                                  42);
  if (name == "manage_buy_offer")
    return ops::manage_buy_offer(Asset::native(), usd(), *Amount::parse("7.5"),
                                 *Price::parse("0.4"));
  if (name == "create_passive_sell_offer")
    return ops::create_passive_sell_offer(usd(), Asset::native(), *Amount::parse("7.5"),
                                          *Price::parse("0.4"));
  if (name == "set_options_all") {
    auto kp = KeyPair::from_seed(Bytes(32, 0x5a));
    auto key = xdr::SignerKey::signerKeyTypeEd25519(xdr::uint256{kp->public_key()});
    return set_options()
        .inflation_destination(src)
        .clear_flags(1)
        .set_flags(3)
        .master_weight(5)
        .low_threshold(1)
        .med_threshold(2)
        .high_threshold(3)
        .home_domain("example.com")
        .signer(key, 7)
        .build();
  }
  if (name == "set_options_min") return set_options().master_weight(0).build();
  if (name == "change_trust") return ops::change_trust(usd(), *Amount::parse("1000"));
  if (name == "change_trust_pool") {
    auto xa = eur().to_xdr();
    auto xb = usd().to_xdr();
    STELLAR_TRY(xa);
    STELLAR_TRY(xb);
    xdr::ChangeTrustAsset line = xdr::ChangeTrustAsset::assetTypePoolShare(
        xdr::LiquidityPoolParameters::liquidityPoolConstantProduct(
            xdr::LiquidityPoolConstantProductParameters{*xa, *xb, 30}));
    return ops::change_trust_asset(line, *Amount::parse("1000"));
  }
  if (name == "account_merge") return ops::account_merge(src);
  if (name == "manage_data") {
    const char* v = "hello world";
    return ops::manage_data("key1", Bytes{v, v + 11});
  }
  if (name == "manage_data_delete") return ops::manage_data("key2", {});
  if (name == "bump_sequence") return ops::bump_sequence(999999999);
  if (name == "create_claimable_balance")
    return ops::create_claimable_balance(usd(), *Amount::parse("5.5"),
                                         {*ops::claimant_v0(src, unconditional())});
  if (name == "clawback") return ops::clawback(usd(), src, *Amount::parse("1"));
  if (name == "clawback_claimable_balance") {
    auto hb = hex_decode("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    xdr::Hash h{};
    std::copy(hb->begin(), hb->end(), h.begin());
    return ops::clawback_claimable_balance(xdr::ClaimableBalanceID::claimableBalanceIdTypeV0(h));
  }
  if (name == "begin_sponsoring") return ops::begin_sponsoring_future_reserves(src);
  if (name == "end_sponsoring") return ops::end_sponsoring_future_reserves(&src);
  if (name == "revoke_sponsorship_account") {
    auto mx = src.to_muxed_account();
    STELLAR_TRY(mx);
    xdr::uint256 pk{};
    if (mx->type() == xdr::CryptoKeyType::KEY_TYPE_MUXED_ED25519)
      pk = mx->med25519().ed25519;
    else
      pk = mx->ed25519();
    return ops::revoke_sponsorship_ledger_entry(
        xdr::LedgerKey::account(xdr::LedgerKeyAccount{xdr::PublicKey::publicKeyTypeEd25519(pk)}));
  }
  if (name == "lp_deposit") {
    auto pool = Asset::liquidity_pool_id(eur(), usd());
    STELLAR_TRY(pool);
    return ops::liquidity_pool_deposit(*pool, *Amount::parse("10"), *Amount::parse("10"),
                                       Price{1, 2}, Price{3, 4});
  }
  if (name == "lp_withdraw") {
    auto pool = Asset::liquidity_pool_id(eur(), usd());
    STELLAR_TRY(pool);
    return ops::liquidity_pool_withdraw(*pool, *Amount::parse("5"), *Amount::parse("1"),
                                        *Amount::parse("1"));
  }
  return Error::invalid_argument("unknown case " + name);
}

// Build the same envelope the JS generator produced for `name`.
Result<TransactionEnvelope> build_envelope(const std::string& name, const Address& src) {
  auto b = base();
  if (name == "memo_text")
    b.memo(Memo::text("hello world"));
  else if (name == "memo_id")
    b.memo(Memo::id(999999999999));
  else if (name == "memo_hash" || name == "memo_return") {
    std::array<std::uint8_t, 32> h{};
    h.fill(0xde);
    b.memo(name == "memo_hash" ? Memo::hash(h) : Memo::return_(h));
  } else if (name == "precond_v2") {
    b.ledger_bounds(500, 900).min_sequence_age(3600).min_sequence_ledger_gap(5).min_sequence_number(
        1234567890123456789LL);
    xdr::Hash sh{};
    for (auto& b8 : sh) b8 = 7;
    b.extra_signer(xdr::SignerKey::signerKeyTypeHashX(sh));
  }
  if (name == "multi_op") {
    // JS `fee` is per-operation: 3 ops -> 300 total
    b.fee(300);
    b.add_operation(*ops::payment(src, usd(), *Amount::parse("1")));
    b.add_operation(*ops::manage_data("k", Bytes{'v'}));
    b.add_operation(*ops::payment(src, eur(), *Amount::parse("2"), &src));
  } else {
    const bool memo_case = name.rfind("memo_", 0) == 0;
    const bool default_op = memo_case || name == "precond_v2" || name == "fee_bump";
    auto op = build_op(default_op ? "payment_1xlm" : name, src);
    STELLAR_TRY(op);
    if (name == "fee_bump") return Error::invalid_argument("fee_bump handled separately");
    b.add_operation(*op);
  }
  STELLAR_TRY_ASSIGN(auto tx, b.build());
  auto env = TransactionEnvelope::of_transaction(tx);
  auto signer = src_signer();
  STELLAR_TRY(env.sign(signer, kNet));
  return env;
}

}  // namespace

TEST(CrossSdk, JSEnvelopesByteIdentical) {
  std::ifstream f(std::string(STELLAR_FIXTURES_DIR) + "/../cross_sdk/fixtures_js.json");
  ASSERT_TRUE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  auto doc = Json::parse(ss.str());
  ASSERT_TRUE(doc.ok());
  const auto src = src_addr();

  const auto& cases = doc->at("cases").as_array();
  ASSERT_EQ(cases.size(), 32u);
  for (const auto& c : cases) {
    const std::string name = c.at("name").as_string();
    const std::string want_b64 = c.at("envelopeXdr").as_string();
    const std::string want_hash = c.at("txHash").as_string();

    auto env = [&]() -> Result<TransactionEnvelope> {
      if (name == "fee_bump") {
        // inner tx: payment_native signed by the same key
        auto inner = build_envelope("payment_1xlm", src);
        if (!inner.ok()) return inner.error();
        FeeBumpBuilder fb(src);
        fb.fee(10000);
        auto ftx = fb.build(*inner);
        if (!ftx.ok()) return ftx.error();
        auto e = TransactionEnvelope::of_fee_bump(*ftx);
        auto signer = src_signer();
        auto s = e.sign(signer, kNet);
        if (!s.ok()) return s.error();
        return e;
      }
      return build_envelope(name, src);
    }();
    ASSERT_TRUE(env.ok()) << name << ": " << env.error().describe();

    auto mine_b64 = env->to_base64();
    ASSERT_TRUE(mine_b64.ok());
    EXPECT_EQ(*mine_b64, want_b64) << "envelope mismatch for " << name;

    auto mine_hash = env->hash(kNet);
    ASSERT_TRUE(mine_hash.ok());
    EXPECT_EQ(hex_encode(ByteSpan(mine_hash->data(), mine_hash->size())), want_hash)
        << "hash mismatch for " << name;
  }
}
