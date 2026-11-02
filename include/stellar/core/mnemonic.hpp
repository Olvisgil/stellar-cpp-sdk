// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file mnemonic.hpp
/// SEP-5 key derivation: BIP-39 mnemonic seeds -> SLIP-10 Ed25519 derivation
/// at path m/44'/148'/n' (Stellar's designated BIP-44 coin type 148).

#include "stellar/core/keypair.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1::sep5 {

/// Returns the canonical BIP-39 English wordlist (2048 entries).
STELLAR_API const std::vector<std::string_view>& wordlist();

/// Validate a mnemonic phrase: word count must be one of {12,15,18,21,24} and
/// every word must be in the BIP-39 list.
STELLAR_API Result<void> validate_mnemonic(std::string_view phrase);

/// BIP-39 seed (512 bits): PBKDF2-HMAC-SHA512(mnemonic, "mnemonic"+passphrase,
/// 2048 iterations). The passphrase default is "".
STELLAR_API Result<std::array<std::uint8_t, 64>> mnemonic_to_seed(std::string_view phrase,
                                                                  std::string_view passphrase = "");

/// SLIP-10 hardened derivation for Ed25519.
/// path is a list of indices; the hardened bit is added automatically.
STELLAR_API Result<SecretBytes> slip10_ed25519(const std::array<std::uint8_t, 64>& seed,
                                               const std::vector<std::uint32_t>& path);

/// Derive a Stellar keypair from a mnemonic at derivation index `index`
/// (defaults to 0 => m/44'/148'/0').
STELLAR_API Result<KeyPair> keypair_from_mnemonic(std::string_view phrase, std::uint32_t index = 0,
                                                  std::string_view passphrase = "");

}  // namespace stellar::inline v1::sep5
