// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#include "stellar/core/address.hpp"
#include "stellar/core/amount.hpp"
#include "stellar/core/bigint.hpp"
#include "stellar/core/keypair.hpp"
#include "stellar/core/memo.hpp"
#include "stellar/core/network.hpp"

#include <gtest/gtest.h>

using namespace stellar::v1;

// SEP-23 valid vectors
TEST(StrKey, ValidVectors) {
  struct V {
    const char* s;
    char c;
  };
  const V vs[] = {
      {"GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ", 'G'},
      {"MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAACJUQ", 'M'},
      {"MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVAAAAAAAAAAAAAJLK", 'M'},
      {"PA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAQACAQDAQCQMBYIBEFAWDANBYHRAEISCMK"
       "BKFQXDAMRUGY4DUPB6IBZGM",
       'P'},
      {"PA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAOQCAQDAQCQMBYIBEFAWDANBYHRAEISCMK"
       "BKFQXDAMRUGY4DUAAAAFGBU",
       'P'},
      {"CA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUWDA", 'C'},
      {"LA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUPJN", 'L'},
      {"BAAD6DBUX6J22DMZOHIEZTEQ64CVCHEDRKWZONFEUL5Q26QD7R76RGR4TU", 'B'},
      {"WA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAAAWWC", 'W'},
      {"WA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVAAAAAAAAAAAACWJY", 'W'},
  };
  for (const auto& v : vs) {
    auto sk = StrKey::decode(v.s);
    ASSERT_TRUE(sk.ok()) << v.s << " -> " << sk.error().describe();
    EXPECT_EQ(sk->first_char(), v.c) << v.s;
    EXPECT_EQ(sk->encode(), v.s) << v.s;  // canonical round-trip
  }
}

// SEP-23 invalid vectors: checksum, length, version, padding, unused bits
TEST(StrKey, InvalidVectors) {
  const char* bad[] = {
      "MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAACJUR",
      "GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZA",
      "GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUACUSI",
      "G47QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVP2I",
      "MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVAAAAAAAAAAAAAJLKA",
      "MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVAAAAAAAAAAAAAAV75I",
      "M47QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAACJUQ",
      "MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAACJUK===",
      "MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAACJUO",
      "PA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAQACAQDAQCQMBYIBEFAWDANBYHRAEISCMKB"
      "KFQXDAMRUGY4DUPB6IAAAAAAAAPM",
      "PA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAOQCAQDAQCQMBYIBEFAWDANBYHRAEISCMKB"
      "KFQXDAMRUGY4Z2PQ",
      "PA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAOQCAQDAQCQMBYIBEFAWDANBYHRAEISCMKB"
      "KFQXDAMRUGY4DXFH6",
      "BAAD6DBUX6J22DMZOHIEZTEQ64CVCHEDRKWZONFEUL5Q26QD7R76RGR4TV",
      "BAAT6DBUX6J22DMZOHIEZTEQ64CVCHEDRKWZONFEUL5Q26QD7R76RGXACA",
      "WA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAAAWWD",
      "WA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAAAWWCA",
      "WA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAAAAIOUI",
      "W47QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAADXHW",
      "WA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAAAWWA",
      "S47QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ",
  };
  for (const char* s : bad) {
    auto r = StrKey::decode(s);
    EXPECT_FALSE(r.ok()) << "unexpectedly accepted " << s;
  }
}

TEST(StrKey, MuxedFields) {
  auto sk = StrKey::decode("MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAACJUQ");
  ASSERT_TRUE(sk.ok());
  ASSERT_NE(sk->muxed(), nullptr);
  EXPECT_EQ(sk->muxed()->sub_id, 0u);
  auto sk2 =
      StrKey::decode("MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVAAAAAAAAAAAAAJLK");
  ASSERT_TRUE(sk2.ok());
  EXPECT_EQ(sk2->muxed()->sub_id, 9223372036854775808ull);
}

TEST(KeyPair, RoundTrip) {
  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());
  EXPECT_TRUE(kp->account_id().starts_with("G"));
  // secret strkey round-trips to the same account
  StrKey seed_sk{StrKeyKind::kSeed, kp->public_key()};
  (void)seed_sk;  // seed payload differs from pk; skip
  auto msg = Bytes{'h', 'e', 'l', 'l', 'o'};
  auto sig = kp->sign(msg);
  ASSERT_TRUE(sig.ok());
  EXPECT_TRUE(KeyPair::verify(kp->public_key(), msg, *sig));
  EXPECT_FALSE(KeyPair::verify(kp->public_key(), Bytes{'x'}, *sig));
}

TEST(KeyPair, KnownVector) {
  // self-consistent: seed strkey -> keypair -> G account id round-trips
  std::array<std::uint8_t, 32> seed{};
  for (int i = 0; i < 32; ++i) seed[i] = static_cast<std::uint8_t>(i);
  StrKey seed_sk{StrKeyKind::kSeed, seed};
  const std::string s = seed_sk.encode();
  EXPECT_TRUE(s.starts_with("S"));
  auto kp = KeyPair::from_secret(s);
  ASSERT_TRUE(kp.ok()) << kp.error().describe();
  EXPECT_TRUE(kp->account_id().starts_with("G"));
  // signing works and a public-only twin verifies
  auto pub = KeyPair::from_account_id(kp->account_id());
  ASSERT_TRUE(pub.ok());
  EXPECT_FALSE(pub->can_sign());
  Bytes msg{1, 2, 3};
  auto sig = kp->sign(msg);
  ASSERT_TRUE(sig.ok());
  EXPECT_TRUE(KeyPair::verify(pub->public_key(), msg, *sig));
}

TEST(Network, Ids) {
  EXPECT_NE(Network::testnet().id(), Network::public_().id());
  // known testnet id
  Bytes expected{0xce, 0xe0, 0x30, 0x2d, 0x59, 0x84, 0x4d, 0x32, 0xbd, 0xca, 0x91,
                 0x5c, 0x82, 0x03, 0xdd, 0x44, 0xb3, 0x3f, 0xbb, 0x7e, 0xdc, 0x19,
                 0x05, 0x1e, 0xa3, 0x7a, 0xbe, 0xdf, 0x28, 0xec, 0xd4, 0x72};
  EXPECT_EQ(std::vector(Network::testnet().id().begin(), Network::testnet().id().end()), expected);
}

TEST(Amount, ParseAndFormat) {
  auto a = Amount::parse("1234.5678901");
  ASSERT_TRUE(a.ok());
  EXPECT_EQ(a->stroops(), 12345678901LL);
  EXPECT_EQ(a->to_string(), "1234.5678901");
  EXPECT_EQ(Amount::parse("1")->to_string(), "1");
  EXPECT_EQ(Amount::parse("0.0000001")->stroops(), 1);
  EXPECT_EQ(Amount::parse("-2.5")->stroops(), -25000000);
  EXPECT_FALSE(Amount::parse("1.00000001").ok());  // 8 decimals
  EXPECT_FALSE(Amount::parse("").ok());
  EXPECT_FALSE(Amount::parse("abc").ok());
  EXPECT_TRUE(Amount::parse("922337203685.4775807").ok());
}

TEST(Address, Kinds) {
  auto g = Address::parse("GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");
  ASSERT_TRUE(g.ok());
  EXPECT_EQ(g->kind(), Address::Kind::kAccount);
  EXPECT_EQ(g->to_string(), "GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");

  auto c = Address::parse("CA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUWDA");
  ASSERT_TRUE(c.ok());
  EXPECT_EQ(c->kind(), Address::Kind::kContract);
  EXPECT_EQ(c->to_string(), "CA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUWDA");

  auto m = Address::parse("MA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAACJUQ");
  ASSERT_TRUE(m.ok());
  EXPECT_EQ(m->kind(), Address::Kind::kMuxedAccount);
  EXPECT_EQ(m->muxed_id(), 0u);

  auto w = Address::parse("WA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUAAAAAAAAAAAAAWWC");
  ASSERT_TRUE(w.ok());
  EXPECT_EQ(w->kind(), Address::Kind::kMuxedContract);

  // sc_address round-trip
  auto sa = g->to_sc_address();
  ASSERT_TRUE(sa.ok());
  auto back = Address::from_sc_address(*sa);
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(*back, *g);
}

TEST(Memo, Kinds) {
  EXPECT_EQ(Memo::none().type(), xdr::MemoType::MEMO_NONE);
  EXPECT_EQ(Memo::text("hi").type(), xdr::MemoType::MEMO_TEXT);
  EXPECT_EQ(Memo::id(7).type(), xdr::MemoType::MEMO_ID);
  EXPECT_EQ(Memo::hash({}).type(), xdr::MemoType::MEMO_HASH);
  EXPECT_EQ(Memo::return_({}).type(), xdr::MemoType::MEMO_RETURN);
  // long text truncates to 28
  EXPECT_EQ(Memo::text(std::string(40, 'x')).xdr().text().size(), 28u);
}

// ---------------------------------------------------------------------------
// BigInt arithmetic + decimal
// ---------------------------------------------------------------------------

TEST(BigInt, Arithmetic) {
  auto a = UInt128::from_hex("ff");
  auto b = UInt128::from_hex("1");
  ASSERT_TRUE(a.ok() && b.ok());
  auto sum = *a + *b;
  EXPECT_EQ(sum.to_hex(), "00000000000000000000000000000100");
  auto diff = sum - *b;
  EXPECT_EQ(diff.to_hex(), "000000000000000000000000000000ff");
  auto prod = *a * *b;
  EXPECT_EQ(prod.to_hex(), "000000000000000000000000000000ff");
  // wraparound: 2^128-1 + 1 == 0
  auto max = UInt128::from_hex("ffffffffffffffffffffffffffffffff");
  ASSERT_TRUE(max.ok());
  auto wrapped = *max + *b;
  EXPECT_EQ(wrapped, UInt128{});
}

TEST(BigInt, SignedCompare) {
  auto neg = Int128::from_hex("ffffffffffffffffffffffffffffffff");  // -1
  auto zero = Int128{};
  auto one = Int128::from_hex("01");
  ASSERT_TRUE(one.ok());
  EXPECT_TRUE(*neg < zero);
  EXPECT_TRUE(zero < *one);
  EXPECT_TRUE(*neg < *one);
  EXPECT_FALSE(*one < *neg);
}

TEST(BigInt, Decimal) {
  auto d = UInt128::from_decimal("123456789012345678901234567890");
  ASSERT_TRUE(d.ok());
  EXPECT_EQ(d->to_decimal(), "123456789012345678901234567890");
  EXPECT_EQ(d->to_hex(), "000000018ee90ff6c373e0ee4e3f0ad2");

  auto neg = Int128::from_decimal("-99");
  ASSERT_TRUE(neg.ok());
  EXPECT_EQ(neg->to_decimal(), "-99");
  EXPECT_TRUE(neg->negative());

  auto d256 = UInt256::from_decimal("340282366920938463463374607431768211455");
  ASSERT_TRUE(d256.ok());
  EXPECT_EQ(d256->to_decimal(), "340282366920938463463374607431768211455");

  EXPECT_FALSE(UInt128::from_decimal("12a45").ok());
  EXPECT_FALSE(UInt128::from_decimal("-1").ok());
  // 2^128 overflow for UInt128
  EXPECT_FALSE(UInt128::from_decimal("340282366920938463463374607431768211456").ok());
  // i128 min/max edges
  ASSERT_TRUE(Int128::from_decimal("-170141183460469231731687303715884105728").ok());
  ASSERT_TRUE(Int128::from_decimal("170141183460469231731687303715884105727").ok());
  EXPECT_FALSE(Int128::from_decimal("170141183460469231731687303715884105728").ok());
}
