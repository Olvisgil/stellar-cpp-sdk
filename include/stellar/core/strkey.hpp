// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file strkey.hpp
/// SEP-23 strkeys: versioned, CRC16-checksummed base32 encodings for every
/// address/signer/secret kind (G, M, S, T, X, P, C, L, B, W).

#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace stellar::inline v1 {

/// Strkey payload kinds and their SEP-23 version bytes.
enum class StrKeyKind : std::uint8_t {
  kAccount = 6 << 3,           // G - ed25519 public key
  kMuxedAccount = 12 << 3,     // M - muxed ed25519 account
  kSeed = 18 << 3,             // S - ed25519 secret seed
  kPreAuthTx = 19 << 3,        // T - pre-authorized tx hash
  kHashX = 23 << 3,            // X - sha256 hash signer
  kSignedPayload = 15 << 3,    // P - ed25519 signed payload
  kContract = 2 << 3,          // C - contract id
  kLiquidityPool = 11 << 3,    // L - liquidity pool id
  kClaimableBalance = 1 << 3,  // B - claimable balance id
  kMuxedContract = 22 << 3,    // W - muxed contract
};

/// A decoded strkey: kind + raw payload bytes (version/checksum stripped).
class STELLAR_API StrKey {
 public:
  /// Muxed payload: 32-byte key + 64-bit sub-identity (big-endian in payload).
  struct Muxed {
    std::array<std::uint8_t, 32> id;
    std::uint64_t sub_id = 0;
    bool operator==(const Muxed&) const = default;
  };

  using Payload = std::variant<std::array<std::uint8_t, 32>,  // fixed 32
                               Muxed,                         // 40 bytes (M, W)
                               Bytes>;                        // signed payload 64-96 (P)

  StrKey() = default;
  StrKey(StrKeyKind kind, Payload payload) : kind_(kind), payload_(std::move(payload)) {}

  /// Decodes strictly per SEP-23: canonical base32 (no padding, zero unused
  /// bits), exact payload length for the kind, valid CRC16.
  static Result<StrKey> decode(std::string_view text);

  /// Re-encodes canonically. The string always round-trips decode().
  [[nodiscard]] std::string encode() const;

  /// Builds from kind+payload without re-encoding (payload size is validated).
  static Result<StrKey> from_parts(StrKeyKind kind, Payload payload);

  [[nodiscard]] StrKeyKind kind() const noexcept { return kind_; }
  [[nodiscard]] char first_char() const noexcept;
  [[nodiscard]] const Payload& payload() const noexcept { return payload_; }

  /// Convenience accessors (nullptr if kind doesn't match).
  [[nodiscard]] const std::array<std::uint8_t, 32>* fixed() const noexcept {
    return std::get_if<0>(&payload_);
  }
  [[nodiscard]] const Muxed* muxed() const noexcept { return std::get_if<1>(&payload_); }
  [[nodiscard]] const Bytes* signed_payload() const noexcept { return std::get_if<2>(&payload_); }

  bool operator==(const StrKey&) const = default;

 private:
  StrKeyKind kind_;
  Payload payload_;
};

}  // namespace stellar::inline v1
