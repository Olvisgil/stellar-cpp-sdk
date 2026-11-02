// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/contract/scval.hpp"

#include "stellar/xdr/codec.hpp"

#include <algorithm>
#include <cstring>
#include "stellar/core/address.hpp"

namespace stellar::inline v1 {

xdr::SCVal scval::u128(std::uint64_t hi, std::uint64_t lo) {
  return xdr::SCVal::scvU128(xdr::UInt128Parts{hi, lo});
}
xdr::SCVal scval::i128(std::int64_t hi, std::uint64_t lo) {
  return xdr::SCVal::scvI128(xdr::Int128Parts{hi, lo});
}
xdr::SCVal scval::u256(std::uint64_t hh, std::uint64_t hl, std::uint64_t lh, std::uint64_t ll) {
  return xdr::SCVal::scvU256(xdr::UInt256Parts{hh, hl, lh, ll});
}
xdr::SCVal scval::bytes(ByteSpan v) {
  xdr::SCBytes b;
  b.assign(v.begin(), v.end());
  return xdr::SCVal::scvBytes(b);
}
xdr::SCVal scval::symbol(std::string_view s) {
  xdr::SCSymbol sym;
  sym.assign(s.data(), s.size());
  return xdr::SCVal::scvSymbol(sym);
}
xdr::SCVal scval::string(std::string_view s) {
  xdr::SCString str;
  str.assign(s.data(), s.size());
  return xdr::SCVal::scvString(str);
}
xdr::SCVal scval::address(const Address& a) {
  auto sa = a.to_sc_address();
  if (sa.ok()) return xdr::SCVal::scvAddress(*sa);
  return xdr::SCVal::scvVoid();
}
xdr::SCVal scval::vec(std::vector<xdr::SCVal> items) {
  xdr::SCVec v;
  v.assign(items.begin(), items.end());
  return xdr::SCVal::scvVec(xdr::Box<std::optional<xdr::SCVec>>(v));
}
xdr::SCVal scval::map(std::vector<xdr::SCMapEntry> entries) {
  // canonical order: keys sorted by scval_compare (the host rejects unsorted)
  std::sort(entries.begin(), entries.end(), [](const xdr::SCMapEntry& a,
                                               const xdr::SCMapEntry& b) {
    return scval_compare(a.key.get(), b.key.get()) < 0;
  });
  xdr::SCMap m;
  m.assign(entries.begin(), entries.end());
  return xdr::SCVal::scvMap(xdr::Box<std::optional<xdr::SCMap>>(m));
}
xdr::SCVal scval::executable_tag(std::string_view tag) {
  xdr::SCString s;
  s.assign(tag.data(), tag.size());
  return xdr::SCVal::scvExecutableTag(s);
}

// --- scval_get -------------------------------------------------------------

const bool* scval_get::bool_(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_BOOL ? &v.b() : nullptr;
}
const std::uint32_t* scval_get::u32(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_U32 ? &v.u32() : nullptr;
}
const std::int32_t* scval_get::i32(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_I32 ? &v.i32() : nullptr;
}
const std::uint64_t* scval_get::u64(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_U64 ? reinterpret_cast<const std::uint64_t*>(&v.u64())
                                           : nullptr;
}
const std::int64_t* scval_get::i64(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_I64 ? reinterpret_cast<const std::int64_t*>(&v.i64())
                                           : nullptr;
}
const xdr::SCBytes* scval_get::bytes(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_BYTES ? &v.bytes() : nullptr;
}
const xdr::SCString* scval_get::string(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_STRING ? &v.str() : nullptr;
}
const xdr::SCSymbol* scval_get::symbol(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_SYMBOL ? &v.sym() : nullptr;
}
const xdr::SCAddress* scval_get::address(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_ADDRESS ? &v.address() : nullptr;
}
const xdr::SCVec* scval_get::vec(const xdr::SCVal& v) {
  if (v.type() != xdr::SCValType::SCV_VEC) return nullptr;
  const auto& b = v.vec().get();
  return b.has_value() ? &*b : nullptr;
}
const xdr::SCMap* scval_get::map(const xdr::SCVal& v) {
  if (v.type() != xdr::SCValType::SCV_MAP) return nullptr;
  const auto& b = v.map().get();
  return b.has_value() ? &*b : nullptr;
}
const xdr::SCError* scval_get::error(const xdr::SCVal& v) {
  return v.type() == xdr::SCValType::SCV_ERROR ? &v.error() : nullptr;
}

// --- canonical ordering ----------------------------------------------------

namespace {

template <typename T>
int cmp3(const T& a, const T& b) {
  if (a < b) return -1;
  if (b < a) return 1;
  return 0;
}
int cmp_bytes(const Bytes& a, const Bytes& b) {
  const int c = std::memcmp(a.data(), b.data(), std::min(a.size(), b.size()));
  if (c != 0) return c;
  return cmp3(a.size(), b.size());
}
int cmp_parts_u128(const xdr::UInt128Parts& a, const xdr::UInt128Parts& b) {
  if (int c = cmp3(a.hi, b.hi)) return c;
  return cmp3(a.lo, b.lo);
}
int cmp_parts_i128(const xdr::Int128Parts& a, const xdr::Int128Parts& b) {
  if (int c = cmp3(a.hi, b.hi)) return c;
  return cmp3(a.lo, b.lo);
}

}  // namespace

int scval_compare(const xdr::SCVal& a, const xdr::SCVal& b) {
  const auto ta = a.type();
  const auto tb = b.type();
  if (ta != tb) return cmp3(static_cast<int>(ta), static_cast<int>(tb));
  switch (ta) {
    case xdr::SCValType::SCV_BOOL: return cmp3(a.b(), b.b());
    case xdr::SCValType::SCV_U32: return cmp3(a.u32(), b.u32());
    case xdr::SCValType::SCV_I32: return cmp3(a.i32(), b.i32());
    case xdr::SCValType::SCV_U64: return cmp3(a.u64(), b.u64());
    case xdr::SCValType::SCV_I64: return cmp3(a.i64(), b.i64());
    case xdr::SCValType::SCV_TIMEPOINT: return cmp3(a.timepoint(), b.timepoint());
    case xdr::SCValType::SCV_DURATION: return cmp3(a.duration(), b.duration());
    case xdr::SCValType::SCV_U128: return cmp_parts_u128(a.u128(), b.u128());
    case xdr::SCValType::SCV_I128: return cmp_parts_i128(a.i128(), b.i128());
    case xdr::SCValType::SCV_U256: {
      if (int c = cmp3(a.u256().hi_hi, b.u256().hi_hi)) return c;
      if (int c = cmp3(a.u256().hi_lo, b.u256().hi_lo)) return c;
      if (int c = cmp3(a.u256().lo_hi, b.u256().lo_hi)) return c;
      return cmp3(a.u256().lo_lo, b.u256().lo_lo);
    }
    case xdr::SCValType::SCV_I256: {
      if (int c = cmp3(a.i256().hi_hi, b.i256().hi_hi)) return c;
      if (int c = cmp3(a.i256().hi_lo, b.i256().hi_lo)) return c;
      if (int c = cmp3(a.i256().lo_hi, b.i256().lo_hi)) return c;
      return cmp3(a.i256().lo_lo, b.i256().lo_lo);
    }
    case xdr::SCValType::SCV_BYTES: return cmp_bytes(a.bytes(), b.bytes());
    case xdr::SCValType::SCV_STRING: return cmp3(a.str(), b.str());
    case xdr::SCValType::SCV_SYMBOL: return cmp3(a.sym(), b.sym());
    case xdr::SCValType::SCV_ADDRESS:
      // canonical: compare XDR encodings
      {
        const auto ea = xdr::encode(a.address());
        const auto eb = xdr::encode(b.address());
        if (ea.ok() && eb.ok()) return cmp_bytes(*ea, *eb);
        return 0;
      }
    case xdr::SCValType::SCV_VEC: {
      const auto* va = scval_get::vec(a);
      const auto* vb = scval_get::vec(b);
      if (!va || !vb) return cmp3(bool(va), bool(vb));
      const auto n = std::min(va->size(), vb->size());
      for (std::size_t i = 0; i < n; ++i) {
        if (int c = scval_compare((*va)[i], (*vb)[i])) return c;
      }
      return cmp3(va->size(), vb->size());
    }
    case xdr::SCValType::SCV_MAP: {
      const auto* ma = scval_get::map(a);
      const auto* mb = scval_get::map(b);
      if (!ma || !mb) return cmp3(bool(ma), bool(mb));
      const auto n = std::min(ma->size(), mb->size());
      for (std::size_t i = 0; i < n; ++i) {
        if (int c = scval_compare((*ma)[i].key.get(), (*mb)[i].key.get())) return c;
        if (int c = scval_compare((*ma)[i].val.get(), (*mb)[i].val.get())) return c;
      }
      return cmp3(ma->size(), mb->size());
    }
    case xdr::SCValType::SCV_LEDGER_KEY_NONCE: return cmp3(a.nonce_key().nonce, b.nonce_key().nonce);
    case xdr::SCValType::SCV_EXECUTABLE_TAG:
      return cmp3(a.executable_tag(), b.executable_tag());
    default:
      return 0;  // void, error, contract instance, ledger key instance
  }
}

bool scval_lt(const xdr::SCVal& a, const xdr::SCVal& b) { return scval_compare(a, b) < 0; }

std::string scval_to_string(const xdr::SCVal& v) {
  auto hexv = [](const auto& b) {
    return "0x" + hex_encode(ByteSpan(b.data(), b.size()));
  };
  switch (v.type()) {
    case xdr::SCValType::SCV_BOOL: return v.b() ? "true" : "false";
    case xdr::SCValType::SCV_VOID: return "void";
    case xdr::SCValType::SCV_ERROR: return "error";
    case xdr::SCValType::SCV_U32: return std::to_string(v.u32());
    case xdr::SCValType::SCV_I32: return std::to_string(v.i32());
    case xdr::SCValType::SCV_U64: return std::to_string(v.u64());
    case xdr::SCValType::SCV_I64: return std::to_string(v.i64());
    case xdr::SCValType::SCV_TIMEPOINT: return std::to_string(v.timepoint());
    case xdr::SCValType::SCV_DURATION: return std::to_string(v.duration());
    case xdr::SCValType::SCV_U128: {
      const auto& p = v.u128();
      return std::to_string(static_cast<std::uint64_t>(p.hi)) + ":" +
             std::to_string(static_cast<std::uint64_t>(p.lo));
    }
    case xdr::SCValType::SCV_I128: {
      const auto& p = v.i128();
      if (p.hi == 0 || p.hi == -1) return std::to_string(static_cast<std::int64_t>(p.lo));
      return "(" + std::to_string(p.hi) + "," + std::to_string(p.lo) + ")";
    }
    case xdr::SCValType::SCV_U256: {
      const auto& p = v.u256();
      return "0x" + hex_encode(ByteSpan(reinterpret_cast<const std::uint8_t*>(&p), 32));
    }
    case xdr::SCValType::SCV_I256: {
      const auto& p = v.i256();
      return "0x" + hex_encode(ByteSpan(reinterpret_cast<const std::uint8_t*>(&p), 32));
    }
    case xdr::SCValType::SCV_BYTES: return hexv(v.bytes());
    case xdr::SCValType::SCV_STRING: return v.str();
    case xdr::SCValType::SCV_SYMBOL: return v.sym();
    case xdr::SCValType::SCV_ADDRESS: {
      auto a = Address::from_sc_address(v.address());
      return a.ok() ? a->to_string() : "<address>";
    }
    case xdr::SCValType::SCV_VEC: {
      std::string out = "[";
      bool first = true;
      if (const auto* vec = scval_get::vec(v)) {
        for (const auto& e : *vec) {
          out += (first ? "" : ", ");
          out += scval_to_string(e);
          first = false;
        }
      }
      return out + "]";
    }
    case xdr::SCValType::SCV_MAP: {
      std::string out = "{";
      bool first = true;
      if (const auto* m = scval_get::map(v)) {
        for (const auto& e : *m) {
          out += (first ? "" : ", ");
          out += scval_to_string(e.key.get()) + ": " + scval_to_string(e.val.get());
          first = false;
        }
      }
      return out + "}";
    }
    case xdr::SCValType::SCV_CONTRACT_INSTANCE: return "<contract instance>";
    case xdr::SCValType::SCV_LEDGER_KEY_CONTRACT_INSTANCE: return "contract instance key";
    case xdr::SCValType::SCV_LEDGER_KEY_NONCE: return "<nonce key>";
    case xdr::SCValType::SCV_EXECUTABLE_TAG: return "executable tag:" + std::string(v.executable_tag().begin(), v.executable_tag().end());
    default: return "<unknown scval>";
  }
}

}  // namespace stellar::inline v1
