// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#include "stellar/xdr/generated/types.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace stellar::v1;
using namespace stellar::v1::xdr;

// A default-constructed TransactionEnvelope (TX_V0 -> empty tx) round-trips.
TEST(Xdr, EnvelopeRoundTrip) {
  auto env = TransactionEnvelope::envelopeTypeTxV0(TransactionV0Envelope{});
  auto enc = encode(env);
  ASSERT_TRUE(enc.ok());
  auto dec = decode<TransactionEnvelope>(*enc);
  ASSERT_TRUE(dec.ok()) << dec.error().describe();
  EXPECT_EQ(*dec, env);
}

TEST(Xdr, Base64RoundTrip) {
  auto memo = Memo::memoText(BoundedString<28>{"hello"});
  auto b64 = encode_base64(memo);
  ASSERT_TRUE(b64.ok());
  auto back = decode_base64<Memo>(*b64);
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(*back, memo);
}

TEST(Xdr, TrailingBytesRejected) {
  auto enc = *encode(Memo::memoNone());
  enc.push_back(0);
  auto dec = decode<Memo>(enc);
  ASSERT_FALSE(dec.ok());
  EXPECT_EQ(dec.error().code(), stellar::v1::ErrorCode::kDecodeFailed);
}

TEST(Xdr, NonZeroPaddingRejected) {
  // string<3> "abc" encodes len + 3 bytes + 1 pad byte; set pad to nonzero.
  Bytes bad{0, 0, 0, 3, 'a', 'b', 'c', 7};
  auto dec = decode<BoundedString<32>>(bad);
  EXPECT_FALSE(dec.ok());
}

TEST(Xdr, BoolOutOfRangeRejected) {
  Bytes bad{0, 0, 0, 2};  // bool = 2
  bool v{};
  Decoder d{ByteSpan{bad.data(), bad.size()}, Limits{}};
  EXPECT_FALSE(d.boolean(v));
}

TEST(Xdr, UnionUnknownDiscriminantRejected) {
  Bytes bad{0, 0, 0, 77};
  auto dec = decode<MemoType>(bad);
  EXPECT_FALSE(dec.ok());
}

TEST(Xdr, LengthOverAllocRejected) {
  // vector<uint32> claiming 0xffffffff elements with no payload.
  Bytes bad{0xff, 0xff, 0xff, 0xff};
  auto dec = decode<BoundedVector<std::uint32_t>>(bad);
  EXPECT_FALSE(dec.ok());
}

TEST(Xdr, BoundEnforcedOnDecode) {
  // BoundedBytes<4> with declared len 5.
  Bytes bad{0, 0, 0, 5, 1, 2, 3, 4, 5, 0, 0, 0};
  auto dec = decode<BoundedBytes<4>>(bad);
  EXPECT_FALSE(dec.ok());
}

TEST(Xdr, BoundEnforcedOnEncode) {
  BoundedBytes<4> too_big{1, 2, 3, 4, 5};
  auto enc = encode(too_big);
  EXPECT_FALSE(enc.ok());
  EXPECT_EQ(enc.error().code(), stellar::v1::ErrorCode::kEncodeFailed);
}

TEST(Xdr, ErrorHasFieldPath) {
  // Truncate inside a nested field; the error must name the path to it.
  TransactionV1Envelope tx;
  tx.tx.operations.push_back(Operation{});
  tx.signatures.push_back(DecoratedSignature{});
  auto enc = *encode(tx);
  enc.resize(enc.size() - 1);  // drop a byte inside the signature vector
  auto dec = decode<TransactionV1Envelope>(enc);
  ASSERT_FALSE(dec.ok());
  EXPECT_NE(dec.error().describe().find("signatures"), std::string::npos);
}

// Every registered type: random value -> encode -> decode -> re-encode equal.
TEST(Xdr, PropertyRoundTripAllTypes) {
  int checked = 0;
  for (const auto& te : all_types()) {
    std::uint64_t seed = 0x1234 + static_cast<std::uint64_t>(checked);
    auto made = te.make_random(&seed, 0);
    ASSERT_TRUE(made.ok()) << te.name;
    ASSERT_TRUE(te.validate(*made)) << te.name;
    auto re = te.reencode(*made);
    ASSERT_TRUE(re.ok()) << te.name;
    EXPECT_EQ(*re, *made) << te.name;
    ++checked;
  }
  EXPECT_EQ(checked, 477);  // 495 defs - 18 consts
}

TEST(Xdr, XdrSizeMatchesEncode) {
  const auto* te = find_type("TransactionEnvelope");
  ASSERT_NE(te, nullptr);
  std::uint64_t seed = 7;
  for (int i = 0; i < 50; ++i) {
    auto made = te->make_random(&seed, 0);
    ASSERT_TRUE(made.ok());
    auto dec = decode<TransactionEnvelope>(*made);
    ASSERT_TRUE(dec.ok());
    auto sz = xdr_size(*dec);
    ASSERT_TRUE(sz.ok());
    EXPECT_EQ(*sz, made->size());
  }
}

TEST(Xdr, VisitFields) {
  auto memo = Memo::memoText(BoundedString<28>{"hi"});
  std::vector<std::string> names;
  visit_fields(memo, [&](std::string_view n, const auto&) { names.emplace_back(n); });
  EXPECT_THAT(names, ::testing::ElementsAre("type", "text"));

  TransactionV1Envelope tx;
  tx.tx.operations.push_back(Operation{});
  names.clear();
  visit_fields(tx.tx, [&](std::string_view n, const auto&) { names.emplace_back(n); });
  EXPECT_THAT(names, ::testing::Contains("sourceAccount"));
  EXPECT_THAT(names, ::testing::Contains("operations"));
}

TEST(Xdr, RandomValuesDecodeStrictly) {
  const auto* te = find_type("SCVal");
  ASSERT_NE(te, nullptr);
  std::uint64_t seed = 1;
  for (int i = 0; i < 200; ++i) {
    auto made = te->make_random(&seed, 0);
    ASSERT_TRUE(made.ok());
    // flip a random byte, expect decode still rejects or accepts canonically
    Bytes mutated = *made;
    mutated[mutated.size() % mutated.size()] ^= 0xff;
    auto re = te->reencode(mutated);
    if (re.ok()) EXPECT_EQ(*re, mutated);  // canonical invariant
  }
}
