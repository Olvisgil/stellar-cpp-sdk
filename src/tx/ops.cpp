// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/tx/ops.hpp"

namespace stellar::inline v1::ops {

SetOptionsBuilder& SetOptionsBuilder::inflation_destination(const Address& a) {
  if (auto acc = detail::to_account(a); acc.ok()) {
    op_.inflationDest = *acc;
  }
  return *this;
}

SetOptionsBuilder& SetOptionsBuilder::home_domain(std::string_view d) {
  xdr::BoundedString<32> s;
  s.assign(d.data(), d.size() > 32 ? 32 : d.size());
  op_.homeDomain = s;
  return *this;
}

SetOptionsBuilder& SetOptionsBuilder::signer(xdr::SignerKey key, std::uint32_t weight) {
  op_.signer = xdr::Signer{key, weight};
  return *this;
}

}  // namespace stellar::inline v1::ops
