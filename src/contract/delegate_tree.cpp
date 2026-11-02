// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/contract/delegate_tree.hpp"

#include <set>

namespace stellar::inline v1 {

namespace {

bool addr_eq(const xdr::SCAddress& a, const xdr::SCAddress& b) {
  if (a.type() != b.type()) return false;
  if (a.type() == xdr::SCAddressType::SC_ADDRESS_TYPE_ACCOUNT) {
    return a.accountId() == b.accountId();
  }
  if (a.type() == xdr::SCAddressType::SC_ADDRESS_TYPE_CONTRACT) {
    return a.contractId() == b.contractId();
  }
  if (a.type() == xdr::SCAddressType::SC_ADDRESS_TYPE_MUXED_ACCOUNT) {
    return a.muxedAccount() == b.muxedAccount();
  }
  if (a.type() == xdr::SCAddressType::SC_ADDRESS_TYPE_CLAIMABLE_BALANCE) {
    return a.claimableBalanceId() == b.claimableBalanceId();
  }
  return a.liquidityPoolId() == b.liquidityPoolId();
}

// Recursively check: no address repeats within a sibling set and no node is
// an ancestor of itself.
Result<void> validate(const std::vector<DelegateNode>& level,
                      const xdr::SCAddress* ancestor_addr,
                      std::set<const xdr::SCAddress*> ancestors) {
  for (const auto& n : level) {
    for (const auto* a : ancestors) {
      if (addr_eq(n.address, *a)) {
        return Error::invalid_argument("delegate cycle: address is an ancestor");
      }
    }
    ancestors.insert(&n.address);
    STELLAR_TRY(validate(n.children, &n.address, ancestors));
    ancestors.erase(&n.address);
  }
  // sibling uniqueness
  for (std::size_t i = 0; i < level.size(); ++i)
    for (std::size_t j = i + 1; j < level.size(); ++j)
      if (addr_eq(level[i].address, level[j].address))
        return Error::invalid_argument("duplicate sibling address");
  return {};
}

void flatten(const std::vector<DelegateNode>& level,
             xdr::SorobanDelegateSignature* out, std::size_t idx) {
  out[idx].address = level[idx].address;
  out[idx].signature = xdr::Box<xdr::SCVal>(level[idx].signature);
  auto& kids = *out[idx].nestedDelegates;
  kids.assign(level[idx].children.size(), xdr::SorobanDelegateSignature{});
  for (std::size_t i = 0; i < level[idx].children.size(); ++i)
    flatten(level[idx].children, &kids[i], i);
}

}  // namespace

Result<std::vector<xdr::SorobanDelegateSignature>>
build_delegate_tree(std::vector<DelegateNode> roots) {
  STELLAR_TRY(validate(roots, nullptr, {}));
  std::vector<xdr::SorobanDelegateSignature> out(roots.size());
  for (std::size_t i = 0; i < roots.size(); ++i) flatten(roots, &out[i], i);
  return out;
}

Result<xdr::SorobanDelegateSignature>
to_delegate_signature(const DelegateNode& node) {
  std::vector<DelegateNode> one{node};
  STELLAR_TRY_ASSIGN(auto v, build_delegate_tree(std::move(one)));
  return v[0];
}

}  // namespace stellar::inline v1
