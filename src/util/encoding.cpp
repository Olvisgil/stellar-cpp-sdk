// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/util/encoding.hpp"

namespace stellar::inline v1 {

std::string hex_encode(ByteSpan data) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out(data.size() * 2, '\0');
  for (size_t i = 0; i < data.size(); ++i) {
    out[i * 2] = kDigits[data[i] >> 4];
    out[i * 2 + 1] = kDigits[data[i] & 0xf];
  }
  return out;
}

Result<Bytes> hex_decode(std::string_view hex) {
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  if (hex.size() % 2 != 0) {
    return Error::invalid_argument("hex_decode: odd-length input");
  }
  Bytes out(hex.size() / 2);
  for (size_t i = 0; i < out.size(); ++i) {
    int hi = nibble(hex[i * 2]);
    int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return Error::invalid_argument("hex_decode: non-hex character");
    }
    out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return out;
}

std::string base64_encode(ByteSpan data) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    const std::uint32_t b0 = data[i];
    const std::uint32_t b1 = i + 1 < data.size() ? data[i + 1] : 0;
    const std::uint32_t b2 = i + 2 < data.size() ? data[i + 2] : 0;
    const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
    out.push_back(i + 1 < data.size() ? kAlphabet[(triple >> 6) & 0x3f] : '=');
    out.push_back(i + 2 < data.size() ? kAlphabet[triple & 0x3f] : '=');
  }
  return out;
}

Result<Bytes> base64_decode(std::string_view text) {
  static constexpr std::int8_t kTable[128] = {
      /* 0-47  */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
      /* 48-95 */ 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
                  -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
                  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
      /*96-127*/ -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
                  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
  };
  if (text.size() % 4 != 0) {
    return Error::invalid_argument("base64_decode: length not a multiple of 4");
  }
  Bytes out;
  out.reserve(text.size() / 4 * 3);
  const size_t groups = text.size() / 4;
  for (size_t g = 0; g < groups; ++g) {
    const size_t i = g * 4;
    const bool last = g + 1 == groups;
    int pad = 0;
    if (last) {
      if (text[i + 3] == '=') ++pad;
      if (text[i + 2] == '=') ++pad;
    }
    std::uint32_t sextets[4];
    for (int j = 0; j < 4; ++j) {
      const char c = text[i + static_cast<size_t>(j)];
      if (c == '=') {
        if (!last || j < 4 - pad) {
          return Error::invalid_argument("base64_decode: '=' in a non-final position");
        }
        sextets[j] = 0;
        continue;
      }
      const auto uc = static_cast<unsigned char>(c);
      if (uc >= 128 || kTable[uc] < 0) {
        return Error::invalid_argument("base64_decode: character outside the base64 alphabet");
      }
      sextets[j] = static_cast<std::uint32_t>(kTable[uc]);
    }
    const std::uint32_t triple =
        (sextets[0] << 18) | (sextets[1] << 12) | (sextets[2] << 6) | sextets[3];
    if (pad == 0) {
      out.push_back(static_cast<std::uint8_t>(triple >> 16));
      out.push_back(static_cast<std::uint8_t>(triple >> 8));
      out.push_back(static_cast<std::uint8_t>(triple));
    } else if (pad == 1) {
      out.push_back(static_cast<std::uint8_t>(triple >> 16));
      out.push_back(static_cast<std::uint8_t>(triple >> 8));
      if ((sextets[2] & 0x3) != 0) {
        return Error::invalid_argument("base64_decode: non-zero unused bits in final symbol");
      }
    } else {
      out.push_back(static_cast<std::uint8_t>(triple >> 16));
      if ((sextets[1] & 0xf) != 0) {
        return Error::invalid_argument("base64_decode: non-zero unused bits in final symbol");
      }
    }
  }
  return out;
}

std::string base32_encode_nopad(ByteSpan data) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::string out;
  out.reserve((data.size() * 8 + 4) / 5);
  std::uint64_t buffer = 0;
  int bits = 0;
  for (const std::uint8_t byte : data) {
    buffer = (buffer << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      out.push_back(kAlphabet[(buffer >> (bits - 5)) & 0x1f]);
      bits -= 5;
    }
  }
  if (bits > 0) {
    out.push_back(kAlphabet[(buffer << (5 - bits)) & 0x1f]);
  }
  return out;
}

Result<Bytes> base32_decode_nopad(std::string_view text) {
  // RFC 4648 without padding: lengths congruent to 1, 3 or 6 mod 8 can never
  // be produced by an encoder and must be rejected (SEP-23).
  if (text.size() % 8 == 1 || text.size() % 8 == 3 || text.size() % 8 == 6) {
    return Error::invalid_argument("base32_decode_nopad: impossible unpadded length");
  }
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
  };
  Bytes out;
  out.reserve(text.size() * 5 / 8);
  std::uint64_t buffer = 0;
  int bits = 0;
  for (const char c : text) {
    const int v = val(c);
    if (v < 0) {
      return Error::invalid_argument("base32_decode_nopad: character outside the base32 alphabet");
    }
    buffer = (buffer << 5) | static_cast<std::uint64_t>(v);
    bits += 5;
    if (bits >= 8) {
      out.push_back(static_cast<std::uint8_t>((buffer >> (bits - 8)) & 0xff));
      bits -= 8;
    }
  }
  // The final symbols' unused bits must all be zero for a canonical encoding.
  if (bits > 0 && ((buffer & ((1ULL << bits) - 1)) != 0)) {
    return Error::invalid_argument("base32_decode_nopad: non-zero unused bits in final symbol");
  }
  return out;
}

std::uint16_t crc16_xmodem(ByteSpan data) noexcept {
  std::uint16_t crc = 0;
  for (const std::uint8_t byte : data) {
    crc ^= static_cast<std::uint16_t>(byte) << 8;
    for (int i = 0; i < 8; ++i) {
      crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<std::uint16_t>(crc << 1);
    }
  }
  return crc;
}

bool constant_time_equal(ByteSpan a, ByteSpan b) noexcept {
  if (a.size() != b.size()) return false;
  std::uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

}  // namespace stellar::inline v1
