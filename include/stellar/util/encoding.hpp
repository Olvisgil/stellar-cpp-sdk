// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file encoding.hpp
/// Hex, base64, base32 (all strict/canonical), CRC16-XModem and constant-time compare.

#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace stellar::inline v1 {

/// Lowercase hex, no separators.
STELLAR_API std::string hex_encode(ByteSpan data);

/// Strict hex decode: even length, only [0-9a-fA-F]. Whitespace is rejected.
STELLAR_API Result<Bytes> hex_decode(std::string_view hex);

/// RFC 4648 base64 with padding (the encoding Stellar uses for XDR).
STELLAR_API std::string base64_encode(ByteSpan data);

/// Strict base64 decode: standard alphabet, correct padding placement.
/// Non-canonical inputs (bad chars, padding mid-string) are rejected.
STELLAR_API Result<Bytes> base64_decode(std::string_view text);

/// RFC 4648 base32 without padding (the encoding strkeys use).
STELLAR_API std::string base32_encode_nopad(ByteSpan data);

/// Strict base32 decode of an unpadded string. Rejects characters outside the
/// RFC 4648 alphabet and non-canonical final symbols whose unused bits are
/// nonzero — required by SEP-23 to accept exactly one encoding per value.
STELLAR_API Result<Bytes> base32_decode_nopad(std::string_view text);

/// CRC-16/XModem as specified by SEP-23 for strkey checksums.
STELLAR_API std::uint16_t crc16_xmodem(ByteSpan data) noexcept;

/// Constant-time equality for secrets. Timing depends only on the sizes.
STELLAR_API bool constant_time_equal(ByteSpan a, ByteSpan b) noexcept;

}  // namespace stellar::inline v1
