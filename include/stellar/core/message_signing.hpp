// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file message_signing.hpp
/// SEP-53 signing of arbitrary messages with network binding, for later use
/// in SEP-10 anchor authentication and contract invocation workflows.
/// Layout: 0x01 || "stellar" || sha256("sep53") || network_id || ed25519 ||
/// payload_len || payload.

#include "stellar/core/keypair.hpp"
#include "stellar/core/network.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"

#include <array>

namespace stellar::inline v1::sep53 {

/// The bytes that get signed for a SEP-53 message.
STELLAR_API Bytes message_bytes(const Network& n, const std::array<std::uint8_t, 32>& ed25519,
                                ByteSpan payload);

/// Sign a message for `kp` on `n`.
STELLAR_API Result<std::array<std::uint8_t, 64>> sign(const Network& n, const KeyPair& kp,
                                                    ByteSpan payload);

/// Verify a SEP-53 signature for `account` on `n`.
STELLAR_API bool verify(const Network& n, const std::array<std::uint8_t, 32>& ed25519,
                        ByteSpan payload, ByteSpan signature) noexcept;

}  // namespace stellar::inline v1::sep53
