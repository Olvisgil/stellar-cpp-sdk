// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file memo.hpp
/// Transaction memos: friendly constructors over xdr::Memo.

#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace stellar::inline v1 {

class STELLAR_API Memo {
 public:
  Memo() = default;

  static Memo none() { return Memo(xdr::Memo::memoNone()); }
  static Memo text(std::string_view text);
  static Memo id(std::uint64_t id) { return Memo(xdr::Memo::memoId(static_cast<xdr::uint64>(id))); }
  static Memo hash(std::array<std::uint8_t, 32> h) { return Memo(xdr::Memo::memoHash(h)); }
  static Memo return_(std::array<std::uint8_t, 32> h) { return Memo(xdr::Memo::memoReturn(h)); }

  [[nodiscard]] const xdr::Memo& xdr() const noexcept { return m_; }
  [[nodiscard]] xdr::MemoType type() const noexcept { return m_.type(); }

  explicit Memo(xdr::Memo m) : m_(std::move(m)) {}

  bool operator==(const Memo&) const = default;

 private:
  xdr::Memo m_ = xdr::Memo::memoNone();
};

}  // namespace stellar::inline v1
