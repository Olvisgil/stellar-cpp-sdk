// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#include "stellar/util/encoding.hpp"

#include <gtest/gtest.h>

using namespace stellar::v1;

TEST(Hex, RoundTrip) {
  Bytes b{0x00, 0xff, 0x10, 0xab};
  EXPECT_EQ(hex_encode(b), "00ff10ab");
  auto d = hex_decode("00ff10ab");
  ASSERT_TRUE(d.ok());
  EXPECT_EQ(*d, b);
  EXPECT_FALSE(hex_decode("0").ok());
  EXPECT_FALSE(hex_decode("zz").ok());
  EXPECT_FALSE(hex_decode(" 00").ok());
}

TEST(Base64, RoundTripAndStrict) {
  // RFC 4648 vectors
  auto dec = [](std::string_view s) { return base64_decode(s); };
  EXPECT_EQ(base64_encode(Bytes{}), "");
  EXPECT_EQ(base64_encode(Bytes{'f'}), "Zg==");
  EXPECT_EQ(base64_encode(Bytes{'f', 'o'}), "Zm8=");
  EXPECT_EQ(base64_encode(Bytes{'f', 'o', 'o'}), "Zm9v");
  EXPECT_EQ(base64_encode(Bytes{'f', 'o', 'o', 'b'}), "Zm9vYg==");
  EXPECT_EQ(base64_encode(Bytes{'f', 'o', 'o', 'b', 'a'}), "Zm9vYmE=");
  EXPECT_EQ(base64_encode(Bytes{'f', 'o', 'o', 'b', 'a', 'r'}), "Zm9vYmFy");
  EXPECT_EQ(*dec("Zm9vYmFy"), Bytes({'f', 'o', 'o', 'b', 'a', 'r'}));
  EXPECT_FALSE(dec("Zg=").ok());     // bad length
  EXPECT_FALSE(dec("Z===").ok());    // bad padding
  EXPECT_FALSE(dec("Zm9v\n").ok());  // non-alphabet
  EXPECT_FALSE(dec("Zh==").ok());    // non-zero unused bits
  EXPECT_FALSE(dec("Zm=v").ok());    // '=' mid-string
}

TEST(Base32, RoundTripAndStrict) {
  EXPECT_EQ(base32_encode_nopad(Bytes{'f'}), "MY");
  EXPECT_EQ(base32_encode_nopad(Bytes{'f', 'o'}), "MZXQ");
  EXPECT_EQ(base32_encode_nopad(Bytes{'f', 'o', 'o'}), "MZXW6");
  EXPECT_EQ(base32_encode_nopad(Bytes{'f', 'o', 'o', 'b'}), "MZXW6YQ");
  EXPECT_EQ(base32_encode_nopad(Bytes{'f', 'o', 'o', 'b', 'a'}), "MZXW6YTB");
  EXPECT_EQ(base32_encode_nopad(Bytes{'f', 'o', 'o', 'b', 'a', 'r'}), "MZXW6YTBOI");
  EXPECT_EQ(*base32_decode_nopad("MZXW6YTBOI"), Bytes({'f', 'o', 'o', 'b', 'a', 'r'}));
  EXPECT_FALSE(base32_decode_nopad("M").ok());           // len %8 == 1
  EXPECT_FALSE(base32_decode_nopad("MZX").ok());         // len %8 == 3
  EXPECT_FALSE(base32_decode_nopad("MZXW6=").ok());      // padding char
  EXPECT_FALSE(base32_decode_nopad("mzxw6ytboi").ok());  // lowercase non-canonical
  EXPECT_FALSE(base32_decode_nopad("MZXW6YTBOJ").ok());  // nonzero unused bits
}

TEST(Crc16, KnownValue) {
  // CRC-16/XModem("123456789") = 0x31C3
  EXPECT_EQ(crc16_xmodem(Bytes{'1', '2', '3', '4', '5', '6', '7', '8', '9'}), 0x31c3);
}

TEST(ConstantTimeEqual, Basic) {
  EXPECT_TRUE(constant_time_equal(Bytes{1, 2, 3}, Bytes{1, 2, 3}));
  EXPECT_FALSE(constant_time_equal(Bytes{1, 2, 3}, Bytes{1, 2, 4}));
  EXPECT_FALSE(constant_time_equal(Bytes{1, 2}, Bytes{1, 2, 3}));
}
