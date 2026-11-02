// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file bytes.hpp
/// Common byte aliases used across the SDK.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stellar::inline v1 {

using Bytes = std::vector<std::uint8_t>;
using ByteSpan = std::span<const std::uint8_t>;
using MutableByteSpan = std::span<std::uint8_t>;

}  // namespace stellar::inline v1
