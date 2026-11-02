// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#include "stellar/core/asset.hpp"
#include "stellar/core/bigint.hpp"
#include "stellar/core/keypair.hpp"
#include "stellar/core/message_signing.hpp"
#include "stellar/core/mnemonic.hpp"
#include "stellar/core/price.hpp"

#include <gtest/gtest.h>

using namespace stellar::v1;

TEST(Asset, ParseAndFormat) {
  auto n = Asset::parse("native");
  ASSERT_TRUE(n.ok());
  EXPECT_TRUE(n->is_native());
  EXPECT_EQ(n->to_string(), "native");

  auto a = Asset::parse("USD:GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");
  ASSERT_TRUE(a.ok());
  EXPECT_EQ(a->kind(), Asset::Kind::kAlphaNum4);
  EXPECT_EQ(a->to_string(), "USD:GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");

  auto b = Asset::parse("LONGNAME123:GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");
  ASSERT_TRUE(b.ok());
  EXPECT_EQ(b->kind(), Asset::Kind::kAlphaNum12);

  EXPECT_FALSE(Asset::parse("").ok());
  EXPECT_FALSE(Asset::parse("NOISSUER").ok());
  EXPECT_FALSE(Asset::parse("TOOLONGASSETCODE13:X").ok());
}

TEST(Asset, XdrRoundTrip) {
  auto a = Asset::parse("USD:GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");
  ASSERT_TRUE(a.ok());
  auto x = a->to_xdr();
  ASSERT_TRUE(x.ok());
  auto back = Asset::from_xdr(*x);
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(*back, *a);
}

TEST(Asset, OrderingAndPool) {
  auto n = Asset::native();
  auto a = Asset::parse("USD:GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");
  ASSERT_TRUE(a.ok());
  EXPECT_TRUE(n < *a);
  auto id1 = Asset::liquidity_pool_id(n, *a);
  auto id2 = Asset::liquidity_pool_id(*a, n);
  ASSERT_TRUE(id1.ok());
  ASSERT_TRUE(id2.ok());
  EXPECT_EQ(*id1, *id2);  // symmetric — canonical ordering
}

TEST(Asset, SacContractId) {
  auto a = Asset::native();
  auto id = a.sac_contract_id(Network::public_());
  ASSERT_TRUE(id.ok());
  // deterministic
  auto id2 = a.sac_contract_id(Network::public_());
  EXPECT_EQ(*id, *id2);
  // differs on testnet
  auto id3 = a.sac_contract_id(Network::testnet());
  ASSERT_TRUE(id3.ok());
  EXPECT_NE(*id, *id3);
}

TEST(Price, ParseFractions) {
  auto p = Price::parse("1/2");
  ASSERT_TRUE(p.ok());
  EXPECT_EQ(p->n(), 1);
  EXPECT_EQ(p->d(), 2);
  EXPECT_EQ(p->to_string(), "1/2");

  auto q = Price::parse("1.25");
  ASSERT_TRUE(q.ok());
  EXPECT_EQ(q->n(), 5);
  EXPECT_EQ(q->d(), 4);

  auto r = Price::parse("0.333333333");
  ASSERT_TRUE(r.ok());
  // exactly representable as 333333333/10^9 — no approximation needed
  EXPECT_EQ(r->n(), 333333333);
  EXPECT_EQ(r->d(), 1000000000);
  // approximate case: 1/3 over an over-range denominator
  auto br = Price::best_rational(1'000'000'000'000LL, 3'000'000'000'000LL);
  ASSERT_TRUE(br.ok());
  EXPECT_EQ(br->n(), 1);
  EXPECT_EQ(br->d(), 3);

  EXPECT_FALSE(Price::parse("1/0").ok());
  EXPECT_FALSE(Price::parse("").ok());
  EXPECT_FALSE(Price::parse("a/b").ok());
}

TEST(BigInt, HexRoundTrip) {
  auto a = UInt256::from_hex("0xdeadbeef");
  ASSERT_TRUE(a.ok());
  EXPECT_EQ(a->bytes[28], 0xde);
  EXPECT_EQ(a->bytes[31], 0xef);
  EXPECT_EQ(a->to_hex().substr(56), "deadbeef");
  EXPECT_FALSE(Int128::from_hex("").ok());
  EXPECT_TRUE(Int128::from_hex("ff").ok());
}

TEST(Sep53, SignVerify) {
  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());
  Bytes payload{0x01, 0x02, 0x03};
  auto sig = sep53::sign(Network::testnet(), *kp, payload);
  ASSERT_TRUE(sig.ok());
  EXPECT_TRUE(sep53::verify(Network::testnet(), kp->public_key(), payload, *sig));
  // wrong network fails
  EXPECT_FALSE(sep53::verify(Network::public_(), kp->public_key(), payload, *sig));
  // wrong payload fails
  Bytes other{0x04};
  EXPECT_FALSE(sep53::verify(Network::testnet(), kp->public_key(), other, *sig));
}

TEST(Sep5, Bip39Vector) {
  // BIP-39 test vector: known mnemonic + "TREZOR" passphrase -> known seed
  const char* phrase =
      "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon "
      "about";
  auto seed = sep5::mnemonic_to_seed(phrase, "TREZOR");
  ASSERT_TRUE(seed.ok()) << seed.error().describe();
  static const std::uint8_t expect[16] = {0xc5, 0x52, 0x57, 0xc3, 0x60, 0xc0, 0x7c, 0x72,
                                          0x02, 0x9a, 0xeb, 0xc1, 0xb5, 0x3c, 0x05, 0xed};
  EXPECT_TRUE(std::equal(std::begin(expect), std::end(expect), seed->begin()));
  // and the empty-passphrase variant
  auto seed0 = sep5::mnemonic_to_seed(phrase);
  ASSERT_TRUE(seed0.ok());
  static const std::uint8_t expect0[8] = {0x5e, 0xb0, 0x0b, 0xbd, 0xdc, 0xf0, 0x69, 0x08};
  EXPECT_TRUE(std::equal(std::begin(expect0), std::end(expect0), seed0->begin()));
}

TEST(Sep5, KnownAccount) {
  // Well-known SEP-5 test: mnemonic -> derived account
  const char* phrase = "illness spike retreat truth genius clock brain pass fit cave bargain toe";
  auto kp = sep5::keypair_from_mnemonic(phrase);
  ASSERT_TRUE(kp.ok()) << kp.error().describe();
  EXPECT_EQ(kp->account_id(), "GDRXE2BQUC3AZNPVFSCEZ76NJ3WWL25FYFK6RGZGIEKWE4SOOHSUJUJ6");
}

TEST(Sep5, Validation) {
  EXPECT_FALSE(sep5::validate_mnemonic("not a real mnemonic phrase at all").ok());
  EXPECT_FALSE(sep5::validate_mnemonic("abandon abandon abandon").ok());  // 3 words
  EXPECT_TRUE(sep5::validate_mnemonic("abandon abandon abandon abandon abandon abandon abandon "
                                      "abandon abandon abandon abandon about")
                  .ok());
}
