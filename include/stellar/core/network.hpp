// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file network.hpp
/// Network passphrases and their derived 32-byte network IDs.

#include "stellar/export.hpp"
#include "stellar/util/bytes.hpp"

#include <array>
#include <string>
#include <string_view>

namespace stellar::inline v1 {

/// A Stellar network, identified by passphrase. The network ID used in
/// signature payloads is SHA-256(passphrase). There is deliberately no global
/// default: every hash/signature takes a Network explicitly.
class STELLAR_API Network {
 public:
  explicit Network(std::string passphrase);

  [[nodiscard]] const std::string& passphrase() const noexcept { return passphrase_; }
  /// SHA-256(passphrase).
  [[nodiscard]] const std::array<std::uint8_t, 32>& id() const noexcept { return id_; }

  static const Network& public_();
  static const Network& testnet();
  static const Network& futurenet();
  static const Network& standalone();  // "Standalone Network ; February 2017"

  bool operator==(const Network&) const = default;

 private:
  std::string passphrase_;
  std::array<std::uint8_t, 32> id_;
};

/// SHA-256 digest of `data`.
STELLAR_API std::array<std::uint8_t, 32> sha256(ByteSpan data) noexcept;

}  // namespace stellar::inline v1
