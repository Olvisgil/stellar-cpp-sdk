// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#include "stellar/tx/ops.hpp"
#include "stellar/tx/transaction.hpp"

#include <gtest/gtest.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace stellar::v1;

static Address addr(const char* s) {
  auto r = Address::parse(s);
  EXPECT_TRUE(r.ok());
  return *r;
}

static const char* kA = "GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ";
static const char* kB = "GCEZWKCA5VLDNRLN3RPRJMRZOX3Z6G5CHCGSNFHEYVXM3XOJMDS674JZ";

TEST(Tx, BuildSignRoundTrip) {
  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());
  auto src = Address::parse(kp->account_id());
  ASSERT_TRUE(src.ok());

  auto op = ops::payment(addr(kA), Asset::native(), *Amount::parse("100"));
  ASSERT_TRUE(op.ok());

  TransactionBuilder b(*src);
  b.sequence_number(42).fee(200).no_timeout().memo(Memo::text("hello")).add_operation(*op);
  auto tx = b.build();
  ASSERT_TRUE(tx.ok());
  EXPECT_EQ(tx->seqNum, 42);
  EXPECT_EQ(tx->fee, 200u);

  auto env = TransactionEnvelope::of_transaction(*tx);
  const auto pub = kp->public_key();
  const auto hint = kp->signature_hint();
  KeyPairSigner s(std::move(*kp));
  ASSERT_TRUE(env.sign(s, Network::testnet()).ok());
  EXPECT_EQ(env.signatures().size(), 1u);

  // verify the signature against the signature base
  auto base = env.signature_base(Network::testnet());
  ASSERT_TRUE(base.ok());
  const auto& ds = env.signatures().front();
  EXPECT_EQ(std::vector(ds.hint.begin(), ds.hint.end()), std::vector(hint.begin(), hint.end()));
  EXPECT_TRUE(KeyPair::verify(pub, *base, ds.signature));

  // tx hash == signature base for v1
  auto h = env.hash(Network::testnet());
  ASSERT_TRUE(h.ok());
  EXPECT_EQ(*h, *base);

  // base64 round-trip
  auto b64 = env.to_base64();
  ASSERT_TRUE(b64.ok());
  auto env2 = TransactionEnvelope::from_base64(*b64);
  ASSERT_TRUE(env2.ok());
  EXPECT_EQ(env2->signatures().size(), 1u);
  auto h2 = env2->hash(Network::testnet());
  ASSERT_TRUE(h2.ok());
  EXPECT_EQ(*h2, *h);
}

TEST(Tx, PreconditionsV2) {
  auto src = addr(kA);
  auto op = ops::inflation();
  TransactionBuilder b(src);
  b.sequence_number(1)
      .fee(100)
      .time_bounds(1000, 2000)
      .ledger_bounds(10, 20)
      .min_sequence_age(3600)
      .add_operation(op);
  auto tx = b.build();
  ASSERT_TRUE(tx.ok());
  ASSERT_EQ(tx->cond.type(), xdr::PreconditionType::PRECOND_V2);
  const auto& v2 = tx->cond.v2();
  ASSERT_TRUE(v2.timeBounds.has_value());
  EXPECT_EQ(v2.timeBounds->minTime, 1000u);
  EXPECT_EQ(v2.ledgerBounds->maxLedger, 20u);
  EXPECT_EQ(v2.minSeqAge, 3600u);
}

TEST(Tx, RequiresOperation) {
  auto src = addr(kA);
  TransactionBuilder b(src);
  EXPECT_FALSE(b.build().ok());
}

TEST(Tx, FeeBump) {
  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());
  auto src = Address::parse(kp->account_id());
  ASSERT_TRUE(src.ok());

  TransactionBuilder b(*src);
  b.sequence_number(7).fee(100).no_timeout().add_operation(ops::inflation());
  auto tx = b.build();
  ASSERT_TRUE(tx.ok());

  FeeBumpBuilder fb(addr(kB));
  fb.fee(1000);
  auto ftx = fb.build(*tx);
  ASSERT_TRUE(ftx.ok());

  auto env = TransactionEnvelope::of_fee_bump(*ftx);
  EXPECT_EQ(env.type(), xdr::EnvelopeType::ENVELOPE_TYPE_TX_FEE_BUMP);
  KeyPairSigner s(std::move(*kp));
  ASSERT_TRUE(env.sign(s, Network::testnet()).ok());
  EXPECT_EQ(env.signatures().size(), 1u);

  // fee bump signature base differs from inner's
  auto base = env.signature_base(Network::testnet());
  ASSERT_TRUE(base.ok());
  auto inner_env = TransactionEnvelope::of_transaction(*tx);
  auto inner_base = inner_env.signature_base(Network::testnet());
  ASSERT_TRUE(inner_base.ok());
  EXPECT_NE(*base, *inner_base);
}

TEST(Tx, V0EnvelopeSignsLikeConvertedV1) {
  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());

  // build an equivalent v1 tx for comparison
  auto src = Address::parse(kp->account_id());
  ASSERT_TRUE(src.ok());
  TransactionBuilder b(*src);
  b.sequence_number(5)
      .fee(100)
      .memo(Memo::id(7))
      .time_bounds(10, 20)
      .add_operation(ops::inflation());
  auto tx = b.build();
  ASSERT_TRUE(tx.ok());

  // hand-construct the matching v0 envelope
  xdr::TransactionV0 v0;
  v0.sourceAccountEd25519 = xdr::uint256{kp->public_key()};
  v0.fee = 100;
  v0.seqNum = 5;
  v0.timeBounds = xdr::TimeBounds{10, 20};
  v0.memo = xdr::Memo::memoId(7);
  v0.operations.push_back(ops::inflation());
  xdr::TransactionV0Envelope v0env{v0, {}};
  auto bytes = xdr::encode(xdr::TransactionEnvelope::envelopeTypeTxV0(v0env));
  ASSERT_TRUE(bytes.ok());
  auto env2 = TransactionEnvelope::from_bytes(*bytes);
  ASSERT_TRUE(env2.ok());

  // signature base must equal the converted v1's
  auto b0 = env2->signature_base(Network::testnet());
  ASSERT_TRUE(b0.ok());
  auto env1 = TransactionEnvelope::of_transaction(*tx);
  auto b1 = env1.signature_base(Network::testnet());
  ASSERT_TRUE(b1.ok());
  EXPECT_EQ(*b0, *b1);

  // and signing lands in the v0 signature list
  KeyPairSigner s(std::move(*kp));
  ASSERT_TRUE(env2->sign(s, Network::testnet()).ok());
  EXPECT_EQ(env2->signatures().size(), 1u);
  auto b64 = env2->to_base64();
  ASSERT_TRUE(b64.ok());
}

TEST(Tx, ExtraSignerLimitReported) {
  auto src = addr(kA);
  xdr::SignerKey k = xdr::SignerKey::signerKeyTypeEd25519(xdr::uint256{});
  TransactionBuilder b(src);
  b.sequence_number(1)
      .fee(100)
      .no_timeout()
      .add_operation(ops::inflation())
      .extra_signer(k)
      .extra_signer(k)
      .extra_signer(k);  // third exceeds protocol max 2
  auto tx = b.build();
  EXPECT_FALSE(tx.ok());
  // two is fine
  TransactionBuilder b2(src);
  b2.sequence_number(1)
      .fee(100)
      .no_timeout()
      .add_operation(ops::inflation())
      .extra_signer(k)
      .extra_signer(k);
  auto tx2 = b2.build();
  EXPECT_TRUE(tx2.ok());
  EXPECT_EQ(tx2->cond.v2().extraSigners.size(), 2u);
}

TEST(Tx, OperationsCoverKinds) {
  auto a = addr(kA);
  auto usd = Asset::parse("USD:" + std::string(kA));
  ASSERT_TRUE(usd.ok());
  Price p{1, 2};
  std::vector<Asset> path{*usd};
  std::array<std::uint8_t, 32> pool{};

  auto cases = {
      ops::create_account(addr(kB), *Amount::parse("1")),
      ops::payment(addr(kB), *usd, *Amount::parse("5")),
      ops::account_merge(addr(kB)),
      ops::path_payment_strict_receive(*usd, *Amount::parse("1"), addr(kB), Asset::native(),
                                       *Amount::parse("2"), path),
      ops::path_payment_strict_send(*usd, *Amount::parse("1"), addr(kB), Asset::native(),
                                    *Amount::parse("2")),
      ops::manage_sell_offer(*usd, Asset::native(), *Amount::parse("3"), p),
      ops::manage_buy_offer(*usd, Asset::native(), *Amount::parse("3"), p),
      ops::create_passive_sell_offer(*usd, Asset::native(), *Amount::parse("3"), p),
      ops::change_trust(*usd, *Amount::parse("10")),
      ops::allow_trust(addr(kB), *usd, 1),
      ops::set_trust_line_flags(addr(kB), *usd, 1, 2),
      ops::manage_data("key", Bytes{1, 2, 3}),
      ops::clawback(*usd, addr(kB), *Amount::parse("1")),
      ops::begin_sponsoring_future_reserves(addr(kB)),
      ops::revoke_sponsorship_signer(addr(kB),
                                     xdr::SignerKey::signerKeyTypeEd25519(xdr::uint256{})),
  };
  int ok = 0;
  for (const auto& c : cases) ok += c.ok() ? 1 : 0;
  EXPECT_EQ(ok, static_cast<int>(cases.size()));

  // claimable balance needs claimants — the empty-claimants case above must fail
  EXPECT_FALSE(ops::create_claimable_balance(*usd, *Amount::parse("1"), {}).ok());

  // non-Result ops
  auto op1 = ops::inflation();
  auto op2 = ops::bump_sequence(99);
  auto op3 = ops::end_sponsoring_future_reserves();
  auto op4 =
      ops::claim_claimable_balance(xdr::ClaimableBalanceID::claimableBalanceIdTypeV0(xdr::Hash{}));
  auto op5 = ops::clawback_claimable_balance(
      xdr::ClaimableBalanceID::claimableBalanceIdTypeV0(xdr::Hash{}));
  auto op6 = ops::liquidity_pool_deposit(pool, *Amount::parse("1"), *Amount::parse("2"), p, p);
  auto op7 = ops::liquidity_pool_withdraw(pool, *Amount::parse("1"), *Amount::parse("1"),
                                          *Amount::parse("2"));
  auto op8 = ops::extend_footprint_ttl(100);
  auto op9 = ops::restore_footprint();
  auto op10 = ops::revoke_sponsorship_ledger_entry(
      xdr::LedgerKey::account(xdr::LedgerKeyAccount{xdr::PublicKey::publicKeyTypeEd25519({})}));
  auto op11 = ops::set_options().master_weight(1).build();
  EXPECT_EQ(op11.body.type(), xdr::OperationType::SET_OPTIONS);
}

TEST(Tx, TimeBoundsMandatoryUnlessOptedOut) {
  auto src = addr(kA);
  TransactionBuilder b(src);
  b.sequence_number(1).fee(100).add_operation(ops::inflation());
  auto r = b.build();
  EXPECT_FALSE(r.ok());
  EXPECT_TRUE(r.error().describe().find("time bounds") != std::string::npos);

  TransactionBuilder b2(src);
  b2.sequence_number(1).fee(100).no_timeout().add_operation(ops::inflation());
  EXPECT_TRUE(b2.build().ok());
}

TEST(Tx, OperationCountCap) {
  auto src = addr(kA);
  TransactionBuilder b(src);
  b.sequence_number(1).fee(10000).no_timeout();
  for (int i = 0; i < 101; ++i) b.add_operation(ops::inflation());
  auto r = b.build();
  EXPECT_FALSE(r.ok());
  EXPECT_TRUE(r.error().describe().find("100-operation") != std::string::npos);
}

// ---------------------------------------------------------------------------
// CAP-40 signed-payload signatures
// ---------------------------------------------------------------------------

TEST(Tx, SignedPayloadSignature) {
  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());
  auto src = Address::parse(kp->account_id());
  ASSERT_TRUE(src.ok());
  const Bytes payload{0x01, 0x02, 0x03, 0x04, 0x05};
  const Bytes message{0xaa, 0xbb};

  auto key = SignatureHelpers::signed_payload_signer_key(*src, payload);
  ASSERT_TRUE(key.ok());
  EXPECT_EQ(key->type(), xdr::SignerKeyType::SIGNER_KEY_TYPE_ED25519_SIGNED_PAYLOAD);
  EXPECT_EQ(key->ed25519SignedPayload().payload, payload);

  KeyPairSigner signer(std::move(*kp));
  auto d = SignatureHelpers::signed_payload_signature(signer, *key, payload, message);
  ASSERT_TRUE(d.ok());
  // hint = last 4 bytes of the encoded signer key
  auto kb = xdr::encode(*key);
  ASSERT_TRUE(kb.ok());
  const auto n = kb->size();
  EXPECT_EQ(d->hint,
            (xdr::SignatureHint{{(*kb)[n - 4], (*kb)[n - 3], (*kb)[n - 2], (*kb)[n - 1]}}));
  // signature verifies over payload ++ message
  Bytes data(payload.begin(), payload.end());
  data.insert(data.end(), message.begin(), message.end());
  EXPECT_TRUE(KeyPair::verify(kp->public_key(), ByteSpan(data.data(), data.size()), d->signature));

  EXPECT_FALSE(SignatureHelpers::signed_payload_signer_key(*src, {}).ok());
  EXPECT_FALSE(
      SignatureHelpers::signed_payload_signer_key(*src, ByteSpan(Bytes(65).data(), 65)).ok());
}

// ---------------------------------------------------------------------------
// Price — stellar-base best_r vectors
// ---------------------------------------------------------------------------

TEST(Price, JSSDKVectors) {
  struct V {
    const char* in;
    std::int64_t n, d;
  };
  // from stellar-base best_r test vectors
  for (const auto& v : std::vector<V>{
           {"1", 1, 1},
           {"0.1", 1, 10},
           {"1.5", 3, 2},
           {"0.5", 1, 2},
           {"0.99999", 99999, 100000},
           {"0.0000001", 1, 10000000},
           {"0.000000003", 3, 1000000000},
           {"2147483.647", 2147483647, 1000},
           {"0.3333333333333333", 1, 3},
       }) {
    auto p = Price::parse(v.in);
    ASSERT_TRUE(p.ok()) << v.in << ": " << p.error().describe();
    auto x = p->to_xdr();
    EXPECT_EQ(x.n, v.n) << v.in;
    EXPECT_EQ(x.d, v.d) << v.in;
  }
}
