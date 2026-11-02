// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/memo.hpp"

namespace stellar::inline v1 {

Memo Memo::text(std::string_view text) {
  xdr::BoundedString<28> t;
  t.assign(text.data(), text.size() > 28 ? 28 : text.size());
  // per protocol, MEMO_TEXT is truncated to 28 bytes, not rejected
  return Memo(xdr::Memo::memoText(t));
}

}  // namespace stellar::inline v1
