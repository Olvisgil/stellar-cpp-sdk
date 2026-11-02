// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/mnemonic.hpp"

#include <sodium.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace stellar::inline v1::sep5 {

extern const char* const kBip39English[2048];

const std::vector<std::string_view>& wordlist() {
  static const std::vector<std::string_view> wl{kBip39English, kBip39English + 2048};
  return wl;
}

Result<void> validate_mnemonic(std::string_view phrase) {
  std::istringstream ss{std::string(phrase)};
  std::string w;
  int n = 0;
  const auto& wl = wordlist();
  while (ss >> w) {
    ++n;
    if (!std::binary_search(wl.begin(), wl.end(), std::string_view(w))) {
      return Error::invalid_argument("mnemonic word not in BIP-39 wordlist: " + w);
    }
  }
  if (n != 12 && n != 15 && n != 18 && n != 21 && n != 24) {
    return Error::invalid_argument("mnemonic must be 12, 15, 18, 21 or 24 words");
  }
  return {};
}

// HMAC-SHA512 with an arbitrary-length key (init/update/final form).
static std::array<std::uint8_t, 64> hmac_sha512(ByteSpan key, ByteSpan msg) {
  crypto_auth_hmacsha512_state st;
  crypto_auth_hmacsha512_init(&st, key.data(), key.size());
  crypto_auth_hmacsha512_update(&st, msg.data(), msg.size());
  std::array<std::uint8_t, 64> out;
  crypto_auth_hmacsha512_final(&st, out.data());
  sodium_memzero(&st, sizeof(st));
  return out;
}

// PBKDF2-HMAC-SHA512: T1 alone covers dkLen=64 (hLen=64).
static std::array<std::uint8_t, 64> pbkdf2_sha512(ByteSpan password, ByteSpan salt,
                                                std::uint32_t iters) {
  std::vector<std::uint8_t> salt_i(salt.begin(), salt.end());
  salt_i.insert(salt_i.end(), {0, 0, 0, 1});

  auto u = hmac_sha512(password, ByteSpan(salt_i.data(), salt_i.size()));
  std::array<std::uint8_t, 64> t = u;
  for (std::uint32_t i = 1; i < iters; ++i) {
    u = hmac_sha512(password, ByteSpan(u.data(), u.size()));
    for (std::size_t j = 0; j < 64; ++j) t[j] ^= u[j];
  }
  sodium_memzero(u.data(), u.size());
  return t;
}

Result<std::array<std::uint8_t, 64>> mnemonic_to_seed(std::string_view phrase,
                                                    std::string_view passphrase) {
  STELLAR_TRY(validate_mnemonic(phrase));
  const std::string salt = "mnemonic" + std::string(passphrase);
  return pbkdf2_sha512(
      ByteSpan(reinterpret_cast<const std::uint8_t*>(phrase.data()), phrase.size()),
      ByteSpan(reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size()), 2048);
}

Result<SecretBytes> slip10_ed25519(const std::array<std::uint8_t, 64>& seed,
                                   const std::vector<std::uint32_t>& path) {
  static constexpr char curve[] = "ed25519 seed";
  std::array<std::uint8_t, 64> i;
  i = hmac_sha512(ByteSpan(reinterpret_cast<const std::uint8_t*>(curve), sizeof(curve) - 1),
                  ByteSpan(seed.data(), seed.size()));
  std::array<std::uint8_t, 32> key;
  std::array<std::uint8_t, 32> chain;
  std::memcpy(key.data(), i.data(), 32);
  std::memcpy(chain.data(), i.data() + 32, 32);

  std::array<std::uint8_t, 37> msg{};  // 0x00 || key || index
  std::memcpy(msg.data() + 1, key.data(), 32);
  for (const std::uint32_t idx : path) {
    const std::uint32_t hardened = idx | 0x80000000u;
    msg[33] = static_cast<std::uint8_t>(hardened >> 24);
    msg[34] = static_cast<std::uint8_t>(hardened >> 16);
    msg[35] = static_cast<std::uint8_t>(hardened >> 8);
    msg[36] = static_cast<std::uint8_t>(hardened);
    i = hmac_sha512(ByteSpan(chain.data(), chain.size()), ByteSpan(msg.data(), msg.size()));
    std::memcpy(key.data(), i.data(), 32);
    std::memcpy(chain.data(), i.data() + 32, 32);
    std::memcpy(msg.data() + 1, key.data(), 32);
  }
  SecretBytes out{key};
  sodium_memzero(i.data(), i.size());
  sodium_memzero(key.data(), key.size());
  sodium_memzero(chain.data(), chain.size());
  sodium_memzero(msg.data(), msg.size());
  return out;
}

Result<KeyPair> keypair_from_mnemonic(std::string_view phrase, std::uint32_t index,
                                      std::string_view passphrase) {
  STELLAR_TRY_ASSIGN(auto seed, mnemonic_to_seed(phrase, passphrase));
  STELLAR_TRY_ASSIGN(auto key, slip10_ed25519(seed, {44, 148, index}));
  return KeyPair::from_seed(key.bytes());
}

}  // namespace stellar::inline v1::sep5
