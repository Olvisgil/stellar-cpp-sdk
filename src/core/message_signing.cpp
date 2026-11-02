// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/message_signing.hpp"

#include <cstring>

namespace stellar::inline v1::sep53 {

Bytes message_bytes(const Network& n, const std::array<std::uint8_t, 32>& ed25519,
                    ByteSpan payload) {
  Bytes out;
  out.reserve(1 + 7 + 32 + 32 + 32 + 8 + payload.size());
  out.push_back(0x01);  // version byte
  static constexpr char magic[] = "stellar";
  out.insert(out.end(), magic, magic + 7);
  const auto sep = sha256(ByteSpan(reinterpret_cast<const std::uint8_t*>("sep53"), 5));
  out.insert(out.end(), sep.begin(), sep.end());
  out.insert(out.end(), n.id().begin(), n.id().end());
  out.insert(out.end(), ed25519.begin(), ed25519.end());
  std::uint64_t len = payload.size();
  for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>(len >> (8 * i)));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

Result<std::array<std::uint8_t, 64>> sign(const Network& n, const KeyPair& kp, ByteSpan payload) {
  auto m = message_bytes(n, kp.public_key(), payload);
  return kp.sign(ByteSpan(m.data(), m.size()));
}

bool verify(const Network& n, const std::array<std::uint8_t, 32>& ed25519, ByteSpan payload,
            ByteSpan signature) noexcept {
  auto m = message_bytes(n, ed25519, payload);
  return KeyPair::verify(ed25519, ByteSpan(m.data(), m.size()), signature);
}

}  // namespace stellar::inline v1::sep53
