// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/keypair.hpp"

#include <sodium.h>

#include <cstring>

namespace stellar::inline v1 {

namespace {

struct SodiumInit {
  SodiumInit() { sodium_init(); }
};
[[maybe_unused]] SodiumInit g_sodium_init;

Result<void> sodium_ready() {
  if (sodium_init() < 0) return Error::internal("libsodium failed to initialize");
  return {};
}

}  // namespace

std::array<std::uint8_t, 32> sha256(ByteSpan data) noexcept {
  std::array<std::uint8_t, 32> out;
  crypto_hash_sha256(out.data(), data.data(), data.size());
  return out;
}

SecretBytes::SecretBytes(std::array<std::uint8_t, 32> b) : data_(b), empty_(false) {}
SecretBytes::~SecretBytes() { sodium_memzero(data_.data(), data_.size()); }
SecretBytes::SecretBytes(SecretBytes&& o) noexcept : data_(o.data_), empty_(o.empty_) {
  sodium_memzero(o.data_.data(), o.data_.size());
  o.empty_ = true;
}
SecretBytes& SecretBytes::operator=(SecretBytes&& o) noexcept {
  if (this != &o) {
    data_ = o.data_;
    empty_ = o.empty_;
    sodium_memzero(o.data_.data(), o.data_.size());
    o.empty_ = true;
  }
  return *this;
}

Result<KeyPair> KeyPair::random() {
  STELLAR_TRY(sodium_ready());
  KeyPair kp;
  std::array<std::uint8_t, 64> sk64;
  std::array<std::uint8_t, 32> seed;
  crypto_sign_seed_keypair(kp.public_.data(), sk64.data(), seed.data());
  kp.secret_ = SecretBytes(seed);
  sodium_memzero(seed.data(), seed.size());
  sodium_memzero(sk64.data(), sk64.size());
  return kp;
}

Result<KeyPair> KeyPair::from_seed(ByteSpan seed) {
  STELLAR_TRY(sodium_ready());
  if (seed.size() != 32) {
    return Error::invalid_argument("seed must be 32 bytes");
  }
  KeyPair kp;
  std::array<std::uint8_t, 64> sk64;
  std::array<std::uint8_t, 32> seed32;
  std::memcpy(seed32.data(), seed.data(), 32);
  crypto_sign_seed_keypair(kp.public_.data(), sk64.data(), seed32.data());
  kp.secret_ = SecretBytes(seed32);
  sodium_memzero(seed32.data(), seed32.size());
  sodium_memzero(sk64.data(), sk64.size());
  return kp;
}

Result<KeyPair> KeyPair::from_secret(std::string_view seed_str) {
  STELLAR_TRY_ASSIGN(auto sk, StrKey::decode(seed_str));
  if (sk.kind() != StrKeyKind::kSeed) {
    return Error{ErrorCode::kInvalidStrKey, "expected an 'S...' secret seed"};
  }
  return from_seed(ByteSpan(sk.fixed()->data(), 32));
}

Result<KeyPair> KeyPair::from_account_id(std::string_view account) {
  STELLAR_TRY_ASSIGN(auto sk, StrKey::decode(account));
  if (sk.kind() != StrKeyKind::kAccount) {
    return Error{ErrorCode::kInvalidStrKey, "expected a 'G...' account id"};
  }
  KeyPair kp;
  kp.public_ = *sk.fixed();
  return kp;
}

std::string KeyPair::account_id() const {
  StrKey sk{StrKeyKind::kAccount, public_};
  return sk.encode();
}

Result<std::array<std::uint8_t, 64>> KeyPair::sign(ByteSpan message) const {
  if (!can_sign()) {
    return Error{ErrorCode::kCryptoFailed, "cannot sign with a public-only keypair"};
  }
  std::array<std::uint8_t, 64> sig;
  // libsodium's detached API expects a 64-byte secret key (seed||pk).
  std::array<std::uint8_t, 64> sk64;
  std::memcpy(sk64.data(), secret_.bytes().data(), 32);
  std::memcpy(sk64.data() + 32, public_.data(), 32);
  crypto_sign_detached(sig.data(), nullptr, message.data(), message.size(), sk64.data());
  sodium_memzero(sk64.data(), sk64.size());
  return sig;
}

bool KeyPair::verify(const std::array<std::uint8_t, 32>& public_key,
                     ByteSpan message, ByteSpan signature) noexcept {
  if (signature.size() != 64) return false;
  return crypto_sign_verify_detached(signature.data(), message.data(), message.size(),
                                     public_key.data()) == 0;
}

std::array<std::uint8_t, 4> KeyPair::signature_hint() const noexcept {
  return {public_[28], public_[29], public_[30], public_[31]};
}

}  // namespace stellar::inline v1
