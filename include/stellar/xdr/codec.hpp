// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file codec.hpp
/// XDR (RFC 4506) encode/decode engine and public API.
///
/// Per-type serialization lives in Codec<T> specializations: generic ones for
/// containers/primitives in this header, one per named type in generated code.
/// Decoding is strict: canonical padding, bool 0|1, known enum/union
/// discriminants, declared bounds, and a configurable recursion-depth limit —
/// decoder input is always treated as hostile.

#include "stellar/error.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"
#include "stellar/util/encoding.hpp"
#include "stellar/xdr/bounded.hpp"
#include "stellar/xdr/box.hpp"

#include "stellar/export.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace stellar::inline v1::xdr {

/// Resource limits applied while decoding. Decoders never trust declared
/// lengths; these bounds additionally cap total work.
struct Limits {
  /// Maximum recursion into struct/union definitions.
  std::uint32_t depth = 500;
  /// Maximum total input size, in bytes. decode() also rejects longer input.
  std::size_t length = 32u * 1024u * 1024u;
};

class Decoder;

/// Per-type codec contract. Generated code provides a full specialization
/// (declared in each generated header, defined in generated .cpp files) for
/// every named XDR type.
template <typename T>
struct Codec;

/// Tiny deterministic RNG used only by generated `Codec::gen` for property
/// tests and fuzz-corpus seeds (xorshift64*). Not a CSPRNG.
struct XdrRng {
  std::uint64_t state;

  explicit XdrRng(std::uint64_t seed = 0x9e3779b97f4a7c15ULL) : state(seed ? seed : 1) {}

  std::uint64_t next64() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545f4914f6cdd1dULL;
  }
  std::uint32_t next32() { return static_cast<std::uint32_t>(next64() >> 32); }
  std::uint8_t next8() { return static_cast<std::uint8_t>(next64() >> 32); }
  /// Uniform-ish index into [0, n). n must be > 0.
  std::size_t index(std::size_t n) { return static_cast<std::size_t>(next64() % n); }
};

/// Accumulates output bytes and the first validation error.
///
/// A null-Bytes encoder counts bytes without writing them — the implementation
/// behind xdr_size.
class Encoder {
 public:
  explicit Encoder(Bytes& out) : out_(&out) {}
  explicit Encoder(std::nullptr_t) {}

  [[nodiscard]] const Error& error() const { return error_; }
  [[nodiscard]] bool ok() const { return error_.code() == ErrorCode::kOk; }
  /// Total bytes that would have been (or were) written.
  [[nodiscard]] std::size_t written() const { return size_; }
  bool fail(ErrorCode code, std::string message) {
    if (ok()) error_ = Error(code, path_.empty() ? std::move(message) : path_ + ": " + message);
    return false;
  }

  bool u8(std::uint8_t v) {
    if (out_) {
      out_->push_back(0);
      out_->push_back(0);
      out_->push_back(0);
      out_->push_back(v);
    }
    size_ += 4;
    return true;
  }
  bool u32(std::uint32_t v) {
    if (out_) {
      out_->push_back(static_cast<std::uint8_t>(v >> 24));
      out_->push_back(static_cast<std::uint8_t>(v >> 16));
      out_->push_back(static_cast<std::uint8_t>(v >> 8));
      out_->push_back(static_cast<std::uint8_t>(v));
    }
    size_ += 4;
    return true;
  }
  bool i32(std::int32_t v) { return u32(static_cast<std::uint32_t>(v)); }
  bool u64(std::uint64_t v) {
    return u32(static_cast<std::uint32_t>(v >> 32)) && u32(static_cast<std::uint32_t>(v));
  }
  bool i64(std::int64_t v) { return u64(static_cast<std::uint64_t>(v)); }
  bool boolean(bool v) { return u32(v ? 1u : 0u); }

  /// Writes `n` bytes plus zero padding to a 4-byte boundary.
  bool opaque_n(ByteSpan data) {
    if (out_) {
      out_->insert(out_->end(), data.begin(), data.end());
      for (std::size_t pad = (4 - data.size() % 4) % 4; pad > 0; --pad) out_->push_back(0);
    }
    size_ += pad4(data.size());
    return true;
  }

  /// Writes a field: records the path segment for error context.
  template <typename T>
  bool field(const char* name, const T& v) {
    PathGuard g(*this, name);
    return Codec<T>::encode(v, *this);
  }

 private:
  static constexpr std::size_t pad4(std::size_t n) { return (n + 3) / 4 * 4; }

  struct PathGuard {
    PathGuard(Encoder& e, const char* name) : e_(e), prev_(e.path_.size()) {
      if (!e_.path_.empty()) e_.path_ += '.';
      e_.path_ += name;
    }
    ~PathGuard() { e_.path_.resize(prev_); }
    Encoder& e_;
    std::size_t prev_;
  };
  friend struct PathGuard;

  Bytes* out_ = nullptr;
  std::size_t size_ = 0;
  Error error_{ErrorCode::kOk, ""};
  std::string path_;
};

/// Reads input bytes with strict canonical checks.
class Decoder {
 public:
  Decoder(ByteSpan data, const Limits& limits) : cur_(data.data()), end_(data.data() + data.size()), limits_(limits) {}

  [[nodiscard]] std::size_t remaining() const { return static_cast<std::size_t>(end_ - cur_); }
  [[nodiscard]] const Error& error() const { return error_; }
  [[nodiscard]] bool ok() const { return error_.code() == ErrorCode::kOk; }
  bool fail(ErrorCode code, std::string message) {
    if (ok()) error_ = Error(code, path_.empty() ? std::move(message) : path_ + ": " + message);
    return false;
  }

  /// Marks entry into a struct/union definition; enforces Limits::depth.
  [[nodiscard]] bool enter() {
    if (++depth_ > limits_.depth) {
      --depth_;
      return fail(ErrorCode::kDecodeFailed, "nesting exceeds Limits::depth");
    }
    return true;
  }
  void leave() noexcept { --depth_; }
  struct DepthGuard {
    explicit DepthGuard(Decoder& d) : d_(d), ok_(d.enter()) {}
    ~DepthGuard() { if (ok_) d_.leave(); }
    explicit operator bool() const { return ok_; }
    Decoder& d_;
    bool ok_;
  };

  bool u8(std::uint8_t& v) {
    std::uint32_t w;
    if (!u32(w)) return false;
    if (w > 0xff) return fail(ErrorCode::kDecodeFailed, "8-bit value out of range");
    v = static_cast<std::uint8_t>(w);
    return true;
  }
  bool u32(std::uint32_t& v) {
    if (remaining() < 4) return fail(ErrorCode::kDecodeFailed, "unexpected end of input reading uint32");
    v = (static_cast<std::uint32_t>(cur_[0]) << 24) | (static_cast<std::uint32_t>(cur_[1]) << 16) |
        (static_cast<std::uint32_t>(cur_[2]) << 8) | static_cast<std::uint32_t>(cur_[3]);
    cur_ += 4;
    return true;
  }
  bool i32(std::int32_t& v) {
    std::uint32_t w;
    if (!u32(w)) return false;
    v = static_cast<std::int32_t>(w);
    return true;
  }
  bool u64(std::uint64_t& v) {
    std::uint32_t hi, lo;
    if (!u32(hi) || !u32(lo)) return false;
    v = (static_cast<std::uint64_t>(hi) << 32) | lo;
    return true;
  }
  bool i64(std::int64_t& v) {
    std::uint64_t w;
    if (!u64(w)) return false;
    v = static_cast<std::int64_t>(w);
    return true;
  }
  bool boolean(bool& v) {
    std::uint32_t w;
    if (!u32(w)) return false;
    if (w > 1) return fail(ErrorCode::kDecodeFailed, "bool is not 0 or 1");
    v = w == 1;
    return true;
  }

  /// Length-prefix check for variable-length fields: `count` elements whose
  /// minimum encoded size is `elem_min` bytes must fit in what remains.
  bool count_ok(std::uint64_t count, std::uint64_t elem_min) {
    if (elem_min > 0 && count > remaining() / elem_min) {
      return fail(ErrorCode::kDecodeFailed, "declared count cannot fit in remaining input");
    }
    if (count > limits_.length) {
      return fail(ErrorCode::kDecodeFailed, "declared count exceeds Limits::length");
    }
    return true;
  }

  /// Reads exactly n bytes then verifies zero padding to a 4-byte boundary.
  bool opaque_n(std::size_t n, std::vector<std::uint8_t>& out) {
    if (n > remaining()) return fail(ErrorCode::kDecodeFailed, "opaque data extends past end of input");
    out.assign(cur_, cur_ + n);
    cur_ += n;
    return pad4(n);
  }
  /// Reads exactly n bytes into `out` then verifies padding.
  bool opaque_fixed(std::size_t n, std::uint8_t* out) {
    if (n > remaining()) return fail(ErrorCode::kDecodeFailed, "fixed opaque extends past end of input");
    std::memcpy(out, cur_, n);
    cur_ += n;
    return pad4(n);
  }

  /// Reads a field: pushes the path segment for error context.
  template <typename T>
  bool field(const char* name, T& v) {
    PathGuard g(*this, name);
    return Codec<T>::decode(v, *this);
  }
  /// Reads a container element: pushes an "[i]" path segment.
  template <typename T>
  bool element(std::size_t index, T& v) {
    char seg[24];
    std::snprintf(seg, sizeof(seg), "[%zu]", index);
    PathGuard g(*this, seg);
    return Codec<T>::decode(v, *this);
  }

 private:
  bool pad4(std::size_t n) {
    const std::size_t pad = (4 - n % 4) % 4;
    if (pad > remaining()) return fail(ErrorCode::kDecodeFailed, "padding extends past end of input");
    for (std::size_t i = 0; i < pad; ++i) {
      if (cur_[i] != 0) return fail(ErrorCode::kDecodeFailed, "non-zero padding byte");
    }
    cur_ += pad;
    return true;
  }

  struct PathGuard {
    PathGuard(Decoder& d, const char* name) : d_(d), prev_(d.path_.size()) {
      if (!d_.path_.empty()) d_.path_ += '.';
      d_.path_ += name;
    }
    ~PathGuard() { d_.path_.resize(prev_); }
    Decoder& d_;
    std::size_t prev_;
  };
  friend struct PathGuard;

  const std::uint8_t* cur_;
  const std::uint8_t* end_;
  const Limits& limits_;
  std::size_t depth_ = 0;
  Error error_{ErrorCode::kOk, ""};
  std::string path_;
};

// ---------------------------------------------------------------------------
// Primitive codecs
// ---------------------------------------------------------------------------

template <>
struct Codec<bool> {
  static constexpr std::uint64_t kMinSize = 4;
  static bool encode(bool v, Encoder& e) { return e.boolean(v); }
  static bool decode(bool& v, Decoder& d) { return d.boolean(v); }
  static bool gen(bool& v, XdrRng& r, std::uint32_t) {
    v = (r.next32() & 1) != 0;
    return true;
  }
  template <typename F> static void visit_fields(bool, F&&) {}
};

template <typename T>
  requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t>)
struct Codec<T> {
  static constexpr std::uint64_t kMinSize = 4;
  static bool encode(T v, Encoder& e) { return e.u32(static_cast<std::uint32_t>(v)); }
  static bool decode(T& v, Decoder& d) {
    std::uint32_t w;
    if (!d.u32(w)) return false;
    v = static_cast<T>(w);
    return true;
  }
  static bool gen(T& v, XdrRng& r, std::uint32_t) {
    v = static_cast<T>(r.next32());
    return true;
  }
  template <typename F> static void visit_fields(const T&, F&&) {}
};

template <typename T>
  requires(std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>)
struct Codec<T> {
  static constexpr std::uint64_t kMinSize = 8;
  static bool encode(T v, Encoder& e) { return e.u64(static_cast<std::uint64_t>(v)); }
  static bool decode(T& v, Decoder& d) {
    std::uint64_t w;
    if (!d.u64(w)) return false;
    v = static_cast<T>(w);
    return true;
  }
  static bool gen(T& v, XdrRng& r, std::uint32_t) {
    v = static_cast<T>(r.next64());
    return true;
  }
  template <typename F> static void visit_fields(const T&, F&&) {}
};

// ---------------------------------------------------------------------------
// Container codecs
// ---------------------------------------------------------------------------

/// opaque[n]
template <std::size_t N>
struct Codec<std::array<std::uint8_t, N>> {
  static constexpr std::uint64_t kMinSize = (N + 3) / 4 * 4;
  static bool encode(const std::array<std::uint8_t, N>& v, Encoder& e) {
    return e.opaque_n(ByteSpan(v.data(), N));
  }
  static bool decode(std::array<std::uint8_t, N>& v, Decoder& d) {
    return d.opaque_fixed(N, v.data());
  }
  static bool gen(std::array<std::uint8_t, N>& v, XdrRng& r, std::uint32_t) {
    for (auto& b : v) b = r.next8();
    return true;
  }
  template <typename F> static void visit_fields(const std::array<std::uint8_t, N>&, F&&) {}
};

/// T[n] (element is not a byte)
template <typename T, std::size_t N>
struct Codec<std::array<T, N>> {
  static constexpr std::uint64_t kMinSize = N * Codec<T>::kMinSize;
  static bool encode(const std::array<T, N>& v, Encoder& e) {
    for (std::size_t i = 0; i < N; ++i)
      if (!Codec<T>::encode(v[i], e)) return false;
    return true;
  }
  static bool decode(std::array<T, N>& v, Decoder& d) {
    for (std::size_t i = 0; i < N; ++i)
      if (!d.element(i, v[i])) return false;
    return true;
  }
  static bool gen(std::array<T, N>& v, XdrRng& r, std::uint32_t depth) {
    for (auto& x : v)
      if (!Codec<T>::gen(x, r, depth + 1)) return false;
    return true;
  }
  template <typename F> static void visit_fields(const std::array<T, N>&, F&&) {}
};

/// opaque<N>
template <std::uint64_t N>
struct Codec<BoundedBytes<N>> {
  static constexpr std::uint64_t kMinSize = 4;
  static bool encode(const BoundedBytes<N>& v, Encoder& e) {
    if (v.size() > N) return e.fail(ErrorCode::kEncodeFailed, "opaque exceeds its bound");
    return e.u32(static_cast<std::uint32_t>(v.size())) && e.opaque_n(ByteSpan(v.data(), v.size()));
  }
  static bool decode(BoundedBytes<N>& v, Decoder& d) {
    std::uint32_t n;
    if (!d.u32(n)) return false;
    if (n > N) return d.fail(ErrorCode::kDecodeFailed, "opaque exceeds its declared bound");
    if (!d.count_ok(n, 1)) return false;
    return d.opaque_n(n, static_cast<std::vector<std::uint8_t>&>(v));
  }
  static bool gen(BoundedBytes<N>& v, XdrRng& r, std::uint32_t) {
    const std::size_t cap = static_cast<std::size_t>(N < 32 ? N + 1 : 33);
    const std::size_t n = r.index(cap);
    v.resize(n);
    for (auto& b : v) b = r.next8();
    return true;
  }
  template <typename F> static void visit_fields(const BoundedBytes<N>&, F&&) {}
};

/// string<N>
template <std::uint64_t N>
struct Codec<BoundedString<N>> {
  static constexpr std::uint64_t kMinSize = 4;
  static bool encode(const BoundedString<N>& v, Encoder& e) {
    if (v.size() > N) return e.fail(ErrorCode::kEncodeFailed, "string exceeds its bound");
    return e.u32(static_cast<std::uint32_t>(v.size())) &&
           e.opaque_n(ByteSpan(reinterpret_cast<const std::uint8_t*>(v.data()), v.size()));
  }
  static bool decode(BoundedString<N>& v, Decoder& d) {
    std::uint32_t n;
    if (!d.u32(n)) return false;
    if (n > N) return d.fail(ErrorCode::kDecodeFailed, "string exceeds its declared bound");
    if (!d.count_ok(n, 1)) return false;
    v.resize(n);
    return d.opaque_fixed(n, reinterpret_cast<std::uint8_t*>(v.data()));
  }
  static bool gen(BoundedString<N>& v, XdrRng& r, std::uint32_t) {
    static constexpr char kChars[] = "abcdefghijklmnopqrstuvwxyzABC012 ._-";
    const std::size_t cap = static_cast<std::size_t>(N < 32 ? N + 1 : 33);
    const std::size_t n = r.index(cap);
    v.resize(n);
    for (auto& c : v) c = kChars[r.index(sizeof(kChars) - 1)];
    return true;
  }
  template <typename F> static void visit_fields(const BoundedString<N>&, F&&) {}
};

/// T<N>
template <typename T, std::uint64_t N>
struct Codec<BoundedVector<T, N>> {
  static constexpr std::uint64_t kMinSize = 4;
  static bool encode(const BoundedVector<T, N>& v, Encoder& e) {
    if (v.size() > N) return e.fail(ErrorCode::kEncodeFailed, "vector exceeds its bound");
    if (!e.u32(static_cast<std::uint32_t>(v.size()))) return false;
    for (const auto& x : v)
      if (!Codec<T>::encode(x, e)) return false;
    return true;
  }
  static bool decode(BoundedVector<T, N>& v, Decoder& d) {
    std::uint32_t n;
    if (!d.u32(n)) return false;
    if (n > N) return d.fail(ErrorCode::kDecodeFailed, "vector exceeds its declared bound");
    if (!d.count_ok(n, Codec<T>::kMinSize)) return false;
    v.resize(n);
    for (std::size_t i = 0; i < n; ++i)
      if (!d.element(i, v[i])) return false;
    return true;
  }
  static bool gen(BoundedVector<T, N>& v, XdrRng& r, std::uint32_t depth) {
    const std::size_t cap = static_cast<std::size_t>(N < 3 ? N + 1 : 4);
    const std::size_t n = depth > 4 ? 0 : r.index(cap);
    v.resize(n);
    for (auto& x : v)
      if (!Codec<T>::gen(x, r, depth + 1)) return false;
    return true;
  }
  template <typename F> static void visit_fields(const BoundedVector<T, N>&, F&&) {}
};

/// T*
template <typename T>
struct Codec<std::optional<T>> {
  static constexpr std::uint64_t kMinSize = 4;
  static bool encode(const std::optional<T>& v, Encoder& e) {
    if (!e.boolean(v.has_value())) return false;
    return !v || Codec<T>::encode(*v, e);
  }
  static bool decode(std::optional<T>& v, Decoder& d) {
    bool present;
    if (!d.boolean(present)) return false;
    if (!present) {
      v.reset();
      return true;
    }
    if constexpr (std::is_default_constructible_v<T>) {
      v.emplace();
      return Codec<T>::decode(*v, d);
    } else {
      return d.fail(ErrorCode::kDecodeFailed, "optional element is not default-constructible");
    }
  }
  static bool gen(std::optional<T>& v, XdrRng& r, std::uint32_t depth) {
    if (depth > 4 || (r.next32() & 1) == 0) {
      v.reset();
      return true;
    }
    v.emplace();
    return Codec<T>::gen(*v, r, depth + 1);
  }
  template <typename F> static void visit_fields(const std::optional<T>&, F&&) {}
};

/// Recursive-type indirection: delegates to T.
template <typename T>
struct Codec<Box<T>> {
  static constexpr std::uint64_t kMinSize = Codec<T>::kMinSize;
  static bool encode(const Box<T>& v, Encoder& e) { return Codec<T>::encode(v.get(), e); }
  static bool decode(Box<T>& v, Decoder& d) { return Codec<T>::decode(v.get(), d); }
  static bool gen(Box<T>& v, XdrRng& r, std::uint32_t depth) {
    v.get() = T{};
    return Codec<T>::gen(v.get(), r, depth + 1);
  }
  template <typename F> static void visit_fields(const Box<T>& v, F&& f) {
    Codec<T>::visit_fields(v.get(), std::forward<F>(f));
  }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Serializes `v` per RFC 4506. Fails if a bound or discriminant is invalid.
template <typename T>
Result<Bytes> encode(const T& v) {
  Bytes out;
  out.reserve(64);
  Encoder e(out);
  if (!Codec<T>::encode(v, e) || !e.ok()) return e.error();
  return out;
}

/// The number of bytes encode(v) would produce, without allocating them.
/// Validates bounds/discriminants the same way encode() does.
template <typename T>
Result<std::size_t> xdr_size(const T& v) {
  Encoder e(nullptr);
  if (!Codec<T>::encode(v, e) || !e.ok()) return e.error();
  return e.written();
}

/// Decodes exactly one value: all input must be consumed.
template <typename T>
Result<T> decode(ByteSpan data, const Limits& limits = Limits{}) {
  if (data.size() > limits.length) {
    return Error{ErrorCode::kDecodeFailed, "input exceeds Limits::length"};
  }
  Decoder d(data, limits);
  T v;
  if (!Codec<T>::decode(v, d) || !d.ok()) return d.error();
  if (d.remaining() != 0) {
    return Error{ErrorCode::kDecodeFailed, "trailing bytes after decoded value"};
  }
  return v;
}

template <typename T>
Result<T> decode(const Bytes& data, const Limits& limits = Limits{}) {
  return decode<T>(ByteSpan(data.data(), data.size()), limits);
}

/// RFC 4648 base64 of encode(v) — the wire form used by Stellar RPC/Horizon.
template <typename T>
Result<std::string> encode_base64(const T& v) {
  STELLAR_TRY_ASSIGN(Bytes bytes, encode(v));
  return base64_encode(ByteSpan(bytes.data(), bytes.size()));
}

template <typename T>
Result<T> decode_base64(std::string_view text, const Limits& limits = Limits{}) {
  STELLAR_TRY_ASSIGN(Bytes bytes, base64_decode(text));
  return decode<T>(bytes, limits);
}

/// Visits a value's named fields: f(field_name, member) for each struct
/// field; for unions, f(discriminant_name, disc) then f(arm_name, payload);
/// for enums, f(member_name). Generated code provides Codec<T>::visit_fields.
template <typename T, typename F>
void visit_fields(const T& v, F&& f) {
  Codec<T>::visit_fields(v, std::forward<F>(f));
}

// ---------------------------------------------------------------------------
// Type registry: name-addressed decode/round-trip/random-value entry points.
// Populated by generated code; used by tooling, the debug CLI and tests.
// ---------------------------------------------------------------------------

struct TypeEntry {
  const char* name;
  /// Validates: decodes the bytes strictly. Returns ok on success.
  bool (*validate)(ByteSpan data);
  /// Decodes then re-encodes; Result carries the canonical bytes.
  Result<Bytes> (*reencode)(ByteSpan data);
  /// Generates a random value, returns its encoding. Depth is a seed hint.
  Result<Bytes> (*make_random)(std::uint64_t* seed, std::uint32_t depth);
  /// Decodes then calls cb(ctx, field_name) for each top-level field.
  Result<void> (*visit)(ByteSpan data, void* ctx, void (*cb)(void*, const char*));
};

STELLAR_API const TypeEntry* find_type(std::string_view name) noexcept;
STELLAR_API std::span<const TypeEntry> all_types() noexcept;

}  // namespace stellar::inline v1::xdr
