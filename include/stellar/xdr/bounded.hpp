// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file bounded.hpp
/// Containers for XDR variable-length types (T<N>, opaque<N>, string<N>).
///
/// These are plain views over std containers carrying the XDR bound as a type
/// parameter. The bound is enforced by the codec (encode and decode), not at
/// insertion time, so values can be built up incrementally; attempting to
/// encode an over-bound value is an encode error.

#include <cstdint>
#include <string>
#include <vector>

namespace stellar::inline v1::xdr {

/// Sentinel bound for XDR's implicit 2^32-1 maximum.
inline constexpr std::uint64_t kUnbounded = 0xffffffffULL;

/// XDR `T<N>`: a vector that must contain at most N elements on the wire.
template <typename T, std::uint64_t N = kUnbounded>
struct BoundedVector : std::vector<T> {
  using std::vector<T>::vector;
  static constexpr std::uint64_t kMaxSize = N;
};

/// XDR `opaque<N>`: a byte vector of at most N bytes.
template <std::uint64_t N = kUnbounded>
struct BoundedBytes : std::vector<std::uint8_t> {
  using std::vector<std::uint8_t>::vector;
  static constexpr std::uint64_t kMaxSize = N;
};

/// XDR `string<N>`: a byte string of at most N bytes. Not necessarily UTF-8.
template <std::uint64_t N = kUnbounded>
struct BoundedString : std::string {
  using std::string::string;
  static constexpr std::uint64_t kMaxSize = N;
};

}  // namespace stellar::inline v1::xdr
