// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
/// @file delegate_tree.hpp
/// CAP-71-01 Soroban credential delegate trees: construction with the
/// ordering and no-duplicate rules the host enforces.
#include <vector>
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"
namespace stellar::inline v1 {

/// A node in a Soroban delegate tree: one address signature plus children.
struct STELLAR_API DelegateNode {
  xdr::SCAddress address;
  xdr::SCVal signature;  // signed by `address`
  std::vector<DelegateNode> children;
};

/// Build an ordered, duplicate-free delegate tree for an
/// ADDRESS_WITH_DELEGATES credential.
/// Rules enforced (CAP-71-01):
///   * a node may not appear as a descendant of itself (cycles rejected)
///   * siblings are unique (no duplicate addresses at any level)
/// Returns the top-level list to place in `delegates`.
[[nodiscard]] Result<std::vector<xdr::SorobanDelegateSignature>> build_delegate_tree(
    std::vector<DelegateNode> roots);

/// Flatten a tree into `SorobanDelegateSignature` vectors, preserving order.
[[nodiscard]] Result<xdr::SorobanDelegateSignature> to_delegate_signature(const DelegateNode& node);

}  // namespace stellar::inline v1
