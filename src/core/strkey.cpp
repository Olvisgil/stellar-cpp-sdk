// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/strkey.hpp"

#include "stellar/util/encoding.hpp"

#include <cstring>

namespace stellar::inline v1 {

namespace {

struct KindInfo {
  const char* name;
  std::size_t min_size, max_size;
};

// Expected payload sizes per SEP-23. Signed payloads are 32-64 bytes of
// payload after the 32-byte signer => 64..96 total.
KindInfo kind_info(StrKeyKind k) {
  switch (k) {
    case StrKeyKind::kAccount:
    case StrKeyKind::kSeed:
    case StrKeyKind::kPreAuthTx:
    case StrKeyKind::kHashX:
    case StrKeyKind::kContract:
    case StrKeyKind::kLiquidityPool:
      return {"fixed32", 32, 32};
    case StrKeyKind::kClaimableBalance:
      return {"claimable_balance33", 33, 33};  // 1-byte type tag + 32-byte hash
    case StrKeyKind::kMuxedAccount:
    case StrKeyKind::kMuxedContract:
      return {"muxed40", 40, 40};
    case StrKeyKind::kSignedPayload:
      return {"signed_payload", 64, 96};
  }
  return {"unknown", 0, 0};
}

Result<StrKeyKind> kind_of(std::uint8_t version) {
  switch (version) {
    case 6 << 3:
      return StrKeyKind::kAccount;
    case 12 << 3:
      return StrKeyKind::kMuxedAccount;
    case 18 << 3:
      return StrKeyKind::kSeed;
    case 19 << 3:
      return StrKeyKind::kPreAuthTx;
    case 23 << 3:
      return StrKeyKind::kHashX;
    case 15 << 3:
      return StrKeyKind::kSignedPayload;
    case 2 << 3:
      return StrKeyKind::kContract;
    case 11 << 3:
      return StrKeyKind::kLiquidityPool;
    case 1 << 3:
      return StrKeyKind::kClaimableBalance;
    case 22 << 3:
      return StrKeyKind::kMuxedContract;
  }
  return Error{ErrorCode::kInvalidStrKey, "unknown strkey version byte"};
}

}  // namespace

char StrKey::first_char() const noexcept {
  switch (kind_) {
    case StrKeyKind::kAccount:
      return 'G';
    case StrKeyKind::kMuxedAccount:
      return 'M';
    case StrKeyKind::kSeed:
      return 'S';
    case StrKeyKind::kPreAuthTx:
      return 'T';
    case StrKeyKind::kHashX:
      return 'X';
    case StrKeyKind::kSignedPayload:
      return 'P';
    case StrKeyKind::kContract:
      return 'C';
    case StrKeyKind::kLiquidityPool:
      return 'L';
    case StrKeyKind::kClaimableBalance:
      return 'B';
    case StrKeyKind::kMuxedContract:
      return 'W';
  }
  return '?';
}

Result<StrKey> StrKey::decode(std::string_view text) {
  STELLAR_TRY_ASSIGN(
      Bytes raw, base32_decode_nopad(text).with_context("invalid strkey").transform([](Bytes b) {
        return b;
      }));
  if (raw.size() < 3) {
    return Error{ErrorCode::kInvalidStrKey, "strkey too short"};
  }
  STELLAR_TRY_ASSIGN(const StrKeyKind kind, kind_of(raw[0]));
  const auto info = kind_info(kind);
  const std::size_t payload_size = raw.size() - 3;  // version + 2-byte crc
  if (payload_size < info.min_size || payload_size > info.max_size) {
    return Error{ErrorCode::kInvalidStrKey, "strkey payload has wrong length"};
  }
  const std::uint16_t expect_crc = static_cast<std::uint16_t>(raw[raw.size() - 2]) |
                                   (static_cast<std::uint16_t>(raw[raw.size() - 1]) << 8);
  if (crc16_xmodem(ByteSpan(raw.data(), raw.size() - 2)) != expect_crc) {
    return Error{ErrorCode::kInvalidStrKey, "strkey checksum mismatch"};
  }
  StrKey out;
  out.kind_ = kind;
  const std::uint8_t* p = raw.data() + 1;
  switch (kind) {
    case StrKeyKind::kMuxedAccount:
    case StrKeyKind::kMuxedContract: {
      Muxed m;
      std::memcpy(m.id.data(), p, 32);
      m.sub_id = 0;
      for (int i = 0; i < 8; ++i) m.sub_id = (m.sub_id << 8) | p[32 + i];
      out.payload_ = m;
      break;
    }
    case StrKeyKind::kSignedPayload: {
      // [32-byte ed25519][4-byte big-endian length][payload bytes]
      // Per SEP-23, payload may be zero-padded: declared length must fit
      // within the region and every trailing byte must be zero.
      const std::uint32_t declared =
          (static_cast<std::uint32_t>(p[32]) << 24) | (static_cast<std::uint32_t>(p[33]) << 16) |
          (static_cast<std::uint32_t>(p[34]) << 8) | static_cast<std::uint32_t>(p[35]);
      const std::size_t region = payload_size - 36;
      // SEP-23: the payload region is the XDR var-array form — exactly
      // pad4(declared) bytes with zero padding.
      if (declared > 64 || ((declared + 3) / 4 * 4) != region) {
        return Error{ErrorCode::kInvalidStrKey,
                     "signed payload's inner length doesn't match its size"};
      }
      for (std::size_t i = declared; i < region; ++i) {
        if (p[36 + i] != 0) {
          return Error{ErrorCode::kInvalidStrKey, "signed payload has non-zero padding"};
        }
      }
      out.payload_ = Bytes(p, p + payload_size);
      break;
    }
    case StrKeyKind::kClaimableBalance: {
      // 1-byte claimable-balance type tag + 32-byte hash; only V0 exists.
      if (p[0] != 0) {
        return Error{ErrorCode::kInvalidStrKey, "unsupported claimable balance type tag"};
      }
      out.payload_ = Bytes(p, p + payload_size);
      break;
    }
    default: {
      std::array<std::uint8_t, 32> a;
      std::memcpy(a.data(), p, 32);
      out.payload_ = a;
      break;
    }
  }
  return out;
}

std::string StrKey::encode() const {
  Bytes raw;
  raw.push_back(static_cast<std::uint8_t>(kind_));
  std::visit(
      [&](const auto& p) {
        if constexpr (std::is_same_v<std::decay_t<decltype(p)>, std::array<std::uint8_t, 32>>) {
          raw.insert(raw.end(), p.begin(), p.end());
        } else if constexpr (std::is_same_v<std::decay_t<decltype(p)>, Muxed>) {
          raw.insert(raw.end(), p.id.begin(), p.id.end());
          for (int i = 7; i >= 0; --i)
            raw.push_back(static_cast<std::uint8_t>(p.sub_id >> (8 * i)));
        } else {
          raw.insert(raw.end(), p.begin(), p.end());
        }
      },
      payload_);
  const std::uint16_t crc = crc16_xmodem(ByteSpan(raw.data(), raw.size()));
  raw.push_back(static_cast<std::uint8_t>(crc & 0xff));
  raw.push_back(static_cast<std::uint8_t>(crc >> 8));
  return base32_encode_nopad(ByteSpan(raw.data(), raw.size()));
}

Result<StrKey> StrKey::from_parts(StrKeyKind kind, Payload payload) {
  const auto info = kind_info(kind);
  const std::size_t sz = std::visit(
      [](const auto& p) -> std::size_t {
        if constexpr (std::is_same_v<std::decay_t<decltype(p)>, std::array<std::uint8_t, 32>>)
          return 32;
        else if constexpr (std::is_same_v<std::decay_t<decltype(p)>, Muxed>)
          return 40;
        else
          return p.size();
      },
      payload);
  if (sz < info.min_size || sz > info.max_size) {
    return Error{ErrorCode::kInvalidStrKey, "payload size doesn't match strkey kind"};
  }
  // payload variant arm must also match the kind's shape
  const bool muxed = kind == StrKeyKind::kMuxedAccount || kind == StrKeyKind::kMuxedContract;
  const bool bytes = kind == StrKeyKind::kSignedPayload || kind == StrKeyKind::kClaimableBalance;
  if (muxed != std::holds_alternative<Muxed>(payload) ||
      (bytes && !std::holds_alternative<Bytes>(payload)) ||
      (!muxed && !bytes && !std::holds_alternative<std::array<std::uint8_t, 32>>(payload))) {
    return Error{ErrorCode::kInvalidStrKey, "payload shape doesn't match strkey kind"};
  }
  return StrKey{kind, std::move(payload)};
}

}  // namespace stellar::inline v1
