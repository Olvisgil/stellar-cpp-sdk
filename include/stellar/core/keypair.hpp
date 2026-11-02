// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file keypair.hpp
/// Ed25519 keys via libsodium, `S...` secret seeds, and the Signer abstraction.

#include "stellar/core/strkey.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1 {

/// A 32-byte secret that is zeroed on destruction and never printable.
class STELLAR_API SecretBytes {
 public:
  SecretBytes() = default;
  explicit SecretBytes(std::array<std::uint8_t, 32> b);
  ~SecretBytes();  // sodium_memzero
  SecretBytes(const SecretBytes&) = delete;
  SecretBytes& operator=(const SecretBytes&) = delete;
  SecretBytes(SecretBytes&&) noexcept;
  SecretBytes& operator=(SecretBytes&&) noexcept;

  /// Explicit access only — deliberately no to-string/stream operators.
  [[nodiscard]] ByteSpan bytes() const noexcept { return {data_.data(), data_.size()}; }
  [[nodiscard]] bool empty() const noexcept { return empty_; }

 private:
  std::array<std::uint8_t, 32> data_{};
  bool empty_ = true;
};

/// An Ed25519 keypair. The secret half is a SecretBytes.
class STELLAR_API KeyPair {
 public:
  /// Random new keypair.
  static Result<KeyPair> random();
  /// From a `S...` secret seed strkey.
  static Result<KeyPair> from_secret(std::string_view seed);
  /// From a raw 32-byte secret seed.
  static Result<KeyPair> from_seed(ByteSpan seed);
  /// Public-only keypair from a `G...` account strkey.
  static Result<KeyPair> from_account_id(std::string_view account);

  [[nodiscard]] std::string account_id() const;       // G...
  [[nodiscard]] const std::array<std::uint8_t, 32>& public_key() const noexcept { return public_; }
  [[nodiscard]] bool can_sign() const noexcept { return !secret_.empty(); }

  /// Ed25519 sign; fails if this is a public-only keypair.
  [[nodiscard]] Result<std::array<std::uint8_t, 64>> sign(ByteSpan message) const;
  [[nodiscard]] static bool verify(const std::array<std::uint8_t, 32>& public_key,
                                   ByteSpan message,
                                   ByteSpan signature) noexcept;

  /// XDR signature hint: last 4 bytes of the public key.
  [[nodiscard]] std::array<std::uint8_t, 4> signature_hint() const noexcept;

 private:
  std::array<std::uint8_t, 32> public_{};
  SecretBytes secret_;
};

/// Anything that can produce decorated signatures for an account —
/// keypairs, HSMs, KMS, Ledger, remote signers.
class STELLAR_API Signer {
 public:
  virtual ~Signer() = default;
  /// The account's signature hint (last 4 bytes of public key).
  [[nodiscard]] virtual std::array<std::uint8_t, 4> signature_hint() const = 0;
  /// Sign `message` (already domain-hashed by the caller).
  [[nodiscard]] virtual Result<std::array<std::uint8_t, 64>> sign(ByteSpan message) const = 0;
  [[nodiscard]] virtual std::string account_id() const = 0;
};

/// Adapts a KeyPair to the Signer interface.
class STELLAR_API KeyPairSigner final : public Signer {
 public:
  explicit KeyPairSigner(KeyPair kp) : kp_(std::move(kp)) {}
  [[nodiscard]] std::array<std::uint8_t, 4> signature_hint() const override { return kp_.signature_hint(); }
  [[nodiscard]] Result<std::array<std::uint8_t, 64>> sign(ByteSpan m) const override { return kp_.sign(m); }
  [[nodiscard]] std::string account_id() const override { return kp_.account_id(); }
  [[nodiscard]] const KeyPair& keypair() const noexcept { return kp_; }

 private:
  KeyPair kp_;
};

}  // namespace stellar::inline v1
