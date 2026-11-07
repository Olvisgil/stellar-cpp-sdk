// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file stellar_map.hpp
/// The Stellar ↔ ISO 20022 bridge.
///
/// Builders map Payment records (+ Party detail/options) to XML documents:
///   build_pacs008 / _batch   FI-to-FI customer credit transfer
///   build_pain001            customer credit transfer initiation (disbursement)
///   build_pacs002            status report from per-transaction results
///   build_pacs004            payment return
///   build_camt053/054        statement / debit-credit notification
///
/// Readers pull the domain view back out: instructions_from_* recovers
/// Payment+Party per transaction (Stellar-resolvable fields stay optional),
/// status_reports flattens pacs.002/pain.002, and reconcile_instructions
/// matches reports to the sent batch.
///
/// Conventions (mirroring stellar-go-cli's iso20022 package):
///   non-ISO assets   → Ccy="XXX" + SplmtryData/Envlp/Asset{Cd,Issr,Amt}
///   7-dp amounts     → truncated to 5 fraction digits; exact value in SplmtryData
///   Stellar strkeys  → Acct/Prxy/Id (Othr/Id is Max34Text) with proxy type
///   agents w/o BIC   → FinInstnId/Othr/Id fallback
///   ids              → EndToEndId ← tx hash (≤35 chars) or payment id;
///                      memo text → RmtInf/Ustrd; memo id/hash/return → Strd/RfrdDocInf/Nb

#include "stellar/export.hpp"
#include "stellar/iso20022/documents.hpp"
#include "stellar/iso20022/payment.hpp"
#include "stellar/result.hpp"
#include "stellar/util/clock.hpp"

#include <span>
#include <string>
#include <vector>

namespace stellar::inline v1::iso20022 {

/// Default wall clock for the `clock` parameters below (SystemClock).
[[nodiscard]] STELLAR_API const Clock& default_clock();

// ---------------------------------------------------------------------------
// Builders (Payment → XML)
// ---------------------------------------------------------------------------

[[nodiscard]] STELLAR_API Result<std::string> build_pacs008(const Payment& payment,
                                                            const TransferOptions& opts = {},
                                                            const Clock& clock = default_clock());
[[nodiscard]] STELLAR_API Result<std::string> build_pacs008_batch(
    std::span<const CreditTransferInstruction> instructions, const TransferBatchOptions& opts = {},
    const Clock& clock = default_clock());

[[nodiscard]] STELLAR_API Result<std::string> build_pain001(
    std::span<const CreditTransferInstruction> instructions, const TransferBatchOptions& opts = {},
    const Clock& clock = default_clock());

/// One TxInfAndSts per instruction: `tx_status` maps to TxSts
/// (default ACSC when unset), `tx_reason` to StsRsnInf.
[[nodiscard]] STELLAR_API Result<std::string> build_pacs002(
    std::span<const CreditTransferInstruction> instructions, const StatusReportOptions& opts = {},
    const Clock& clock = default_clock());

/// pain.002 is the customer-side twin of pacs.002.
[[nodiscard]] STELLAR_API Result<std::string> build_pain002(
    std::span<const CreditTransferInstruction> instructions, const StatusReportOptions& opts = {},
    const Clock& clock = default_clock());

/// Payment return for previously-sent instructions.
[[nodiscard]] STELLAR_API Result<std::string> build_pacs004(
    std::span<const CreditTransferInstruction> instructions, const TransferBatchOptions& opts = {},
    const Clock& clock = default_clock());

/// A statement entry per payment: Amt/CdtDbtInd (CRDT when `to` == acct
/// owner), BookgDt/ValDt from `created_at`, TxDtls refs from ids/hash.
/// `account` is the account the statement reports on.
[[nodiscard]] STELLAR_API Result<std::string> build_camt053(
    const CashAccount& account, std::span<const CreditTransferInstruction> instructions,
    const StatementOptions& opts = {}, const Clock& clock = default_clock());
[[nodiscard]] STELLAR_API Result<std::string> build_camt054(
    const CashAccount& account, std::span<const CreditTransferInstruction> instructions,
    const StatementOptions& opts = {}, const Clock& clock = default_clock());

// ---------------------------------------------------------------------------
// Readers (XML/Document → domain view)
// ---------------------------------------------------------------------------

/// Recover one CreditTransferInstruction per transaction. Party detail is
/// best-effort; Payment.{from,to} resolve to Address only when the account
/// id parses as a Stellar strkey (Prxy/Id first, then Othr/Id) — non-Stellar
/// parties leave the corresponding field empty.
[[nodiscard]] STELLAR_API Result<std::vector<CreditTransferInstruction>> instructions_from_pacs008(
    const Pacs008& doc);
[[nodiscard]] STELLAR_API Result<std::vector<CreditTransferInstruction>> instructions_from_pain001(
    const Pain001& doc);

/// Flatten a status report (pacs.002 or pain.002) into one StatusReport per
/// transaction.
[[nodiscard]] STELLAR_API Result<std::vector<StatusReport>> status_reports(const Document& doc);

}  // namespace stellar::inline v1::iso20022
