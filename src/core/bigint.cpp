// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/bigint.hpp"

#include "stellar/util/encoding.hpp"

#include <algorithm>

namespace stellar::inline v1 {

template <std::size_t N, bool Signed>
Result<BigInt<N, Signed>> BigInt<N, Signed>::from_hex(std::string_view hex) {
  if (hex.starts_with("0x") || hex.starts_with("0X")) hex.remove_prefix(2);
  if (hex.size() > N * 2 || hex.empty()) {
    return Error::invalid_argument("hex must be 1.." + std::to_string(N * 2) + " digits");
  }
  std::string padded;
  if (hex.size() % 2 != 0) {  // right-aligned values allow odd length
    padded = "0";
    padded += hex;
    hex = padded;
  }
  STELLAR_TRY_ASSIGN(Bytes b, hex_decode(hex));
  if (b.size() > N) return Error::invalid_argument("hex too long");
  BigInt out;
  // right-align
  const std::size_t off = N - b.size();
  std::copy(b.begin(), b.end(), out.bytes.begin() + static_cast<long>(off));
  if (Signed && hex.size() == N * 2) {
    // already full width; sign is whatever the top bit says
  } else if (Signed) {
    // left-pad sign extension
    const bool neg = !b.empty() && (b[0] & 0x80);
    // For non-negative or partial-width values, zero-pad is correct; for a
    // negative hex literal user must provide full width.
    (void)neg;
  }
  return out;
}

template <std::size_t N, bool Signed>
std::string BigInt<N, Signed>::to_hex() const {
  return hex_encode(ByteSpan(bytes.data(), bytes.size()));
}

namespace {

template <std::size_t N>
std::array<std::uint8_t, N> to_le(const std::array<std::uint8_t, N>& be) {
  std::array<std::uint8_t, N> le{};
  for (std::size_t i = 0; i < N; ++i) le[i] = be[N - 1 - i];
  return le;
}
template <std::size_t N>
std::array<std::uint8_t, N> to_be(const std::array<std::uint8_t, N>& le) {
  return to_le(le);  // reverse is its own inverse
}

template <std::size_t N>
std::array<std::uint8_t, N> add(const std::array<std::uint8_t, N>& a,
                                const std::array<std::uint8_t, N>& b) {
  std::array<std::uint8_t, N> r{};
  std::uint16_t c = 0;
  for (std::size_t i = 0; i < N; ++i) {
    const std::uint16_t s = static_cast<std::uint16_t>(a[i]) + b[i] + c;
    r[i] = static_cast<std::uint8_t>(s);
    c = s >> 8;
  }
  return r;
}
template <std::size_t N>
std::array<std::uint8_t, N> negate(const std::array<std::uint8_t, N>& a) {
  // two's complement: invert + 1
  std::array<std::uint8_t, N> inv{};
  for (std::size_t i = 0; i < N; ++i) inv[i] = ~a[i];
  std::array<std::uint8_t, N> one{};
  one[0] = 1;
  return add(inv, one);
}
template <std::size_t N>
std::array<std::uint8_t, N> mul(const std::array<std::uint8_t, N>& a,
                                const std::array<std::uint8_t, N>& b) {
  // schoolbook multiply; keeps only the low N bytes (mod 2^(8N))
  std::array<std::uint8_t, N> r{};
  for (std::size_t i = 0; i < N; ++i) {
    std::uint32_t c = 0;
    for (std::size_t j = 0; j + i < N; ++j) {
      const std::uint32_t s = static_cast<std::uint32_t>(a[i]) * b[j] + r[i + j] + c;
      r[i + j] = static_cast<std::uint8_t>(s);
      c = s >> 8;
    }
  }
  return r;
}
/// Divide a LE value by a u32; returns quotient and leaves remainder.
template <std::size_t N>
std::array<std::uint8_t, N> divmod(const std::array<std::uint8_t, N>& a,
                                   std::uint32_t d, std::uint32_t& rem) {
  std::array<std::uint8_t, N> q{};
  std::uint64_t r = 0;
  for (std::size_t i = N; i-- > 0;) {
    r = (r << 8) | a[i];
    q[i] = static_cast<std::uint8_t>(r / d);
    r %= d;
  }
  rem = static_cast<std::uint32_t>(r);
  return q;
}
/// a * 10 + digit (LE); returns false on overflow. Every byte needs the
/// multiply — a[i]*10 generates carry even with c == 0.
template <std::size_t N>
bool mul_add_10(std::array<std::uint8_t, N>& a, std::uint32_t digit) {
  std::uint32_t c = digit;
  for (std::size_t i = 0; i < N; ++i) {
    const std::uint32_t s = a[i] * 10u + c;
    a[i] = static_cast<std::uint8_t>(s);
    c = s >> 8;
  }
  return c == 0;
}

}  // namespace

template <std::size_t N, bool Signed>
BigInt<N, Signed> BigInt<N, Signed>::operator+(const BigInt& o) const noexcept {
  BigInt r;
  r.bytes = to_be(add(to_le(bytes), to_le(o.bytes)));
  return r;
}
template <std::size_t N, bool Signed>
BigInt<N, Signed> BigInt<N, Signed>::operator-(const BigInt& o) const noexcept {
  BigInt r;
  r.bytes = to_be(add(to_le(bytes), negate(to_le(o.bytes))));
  return r;
}
template <std::size_t N, bool Signed>
BigInt<N, Signed> BigInt<N, Signed>::operator*(const BigInt& o) const noexcept {
  BigInt r;
  r.bytes = to_be(mul(to_le(bytes), to_le(o.bytes)));
  return r;
}
template <std::size_t N, bool Signed>
BigInt<N, Signed> BigInt<N, Signed>::operator-() const noexcept {
  BigInt r;
  r.bytes = to_be(negate(to_le(bytes)));
  return r;
}

template <std::size_t N, bool Signed>
std::string BigInt<N, Signed>::to_decimal() const {
  auto le = to_le(bytes);
  bool neg = false;
  if constexpr (Signed) {
    neg = bytes[0] & 0x80;
    if (neg) le = negate(le);
  }
  std::string out;
  bool nonzero = true;
  while (nonzero) {
    nonzero = false;
    std::uint32_t rem = 0;
    le = divmod<N>(le, 10, rem);
    for (const auto b : le) nonzero |= b != 0;
    out += static_cast<char>('0' + rem);
  }
  if (out.empty()) out = "0";
  if (neg) out += '-';
  std::reverse(out.begin(), out.end());
  return out;
}

template <std::size_t N, bool Signed>
Result<BigInt<N, Signed>> BigInt<N, Signed>::from_decimal(std::string_view dec) {
  if (dec.empty()) return Error::invalid_argument("empty decimal string");
  bool neg = false;
  if (dec.front() == '-') {
    if constexpr (!Signed) return Error::invalid_argument("negative value in unsigned type");
    neg = true;
    dec.remove_prefix(1);
  }
  if (dec.empty() ||
      dec.find_first_not_of("0123456789") != std::string_view::npos) {
    return Error::invalid_argument("not a decimal number");
  }
  std::array<std::uint8_t, N> acc{};
  for (const char c : dec) {
    if (!mul_add_10<N>(acc, static_cast<std::uint32_t>(c - '0'))) {
      return Error::invalid_argument("decimal overflow");
    }
  }
  // range-check against the type's bounds
  if constexpr (Signed) {
    const bool top = acc[N - 1] & 0x80;
    if (neg) {
      acc = negate(acc);
      // -MIN is representable; anything else must have top bit 1 after negate
      if (!(acc[N - 1] & 0x80)) return Error::invalid_argument("decimal underflow");
    } else if (top) {
      return Error::invalid_argument("decimal overflow");
    }
  }
  BigInt out;
  out.bytes = to_be(acc);
  return out;
}

template class BigInt<16, true>;
template class BigInt<16, false>;
template class BigInt<32, true>;
template class BigInt<32, false>;

}  // namespace stellar::inline v1
