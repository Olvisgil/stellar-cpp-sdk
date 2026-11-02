// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/network.hpp"

#include "stellar/core/keypair.hpp"  // sha256

namespace stellar::inline v1 {

Network::Network(std::string passphrase)
    : passphrase_(std::move(passphrase)),
      id_(sha256(ByteSpan(reinterpret_cast<const std::uint8_t*>(passphrase_.data()),
                          passphrase_.size()))) {}

const Network& Network::public_() {
  static const Network n{"Public Global Stellar Network ; September 2015"};
  return n;
}
const Network& Network::testnet() {
  static const Network n{"Test SDF Network ; September 2015"};
  return n;
}
const Network& Network::futurenet() {
  static const Network n{"Test SDF Future Network ; October 2022"};
  return n;
}
const Network& Network::standalone() {
  static const Network n{"Standalone Network ; February 2017"};
  return n;
}

}  // namespace stellar::inline v1
