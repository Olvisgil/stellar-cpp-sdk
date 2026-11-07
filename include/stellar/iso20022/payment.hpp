// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file payment.hpp
/// Stellar-facing domain types for the ISO 20022 bridge: a neutral Payment
/// record (from/to/asset/amount/memo/ids), the Party detail an ISO message
/// needs, batch instructions, builder options, and the flattened
/// StatusReport/reconciliation views used to match bank-side reports back to
/// sent payments.

#include "stellar/core/address.hpp"
#include "stellar/core/amount.hpp"
#include "stellar/core/asset.hpp"
#include "stellar/core/memo.hpp"
#include "stellar/export.hpp"
#include "stellar/iso20022/documents.hpp"
#include "stellar/util/clock.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stellar::inline v1::iso20022 {

// ---------------------------------------------------------------------------
// Payment — the Stellar-side record every builder consumes
// ---------------------------------------------------------------------------

/// One Stellar payment in ISO terms. `from`/`to`/`asset`/`amount` are the
/// on-chain facts; `id` is the caller's reference (Horizon/RPC payment id,
/// or any unique string); `tx_hash` becomes EndToEndId; `created_at` feeds
/// IntrBkSttlmDt/BookgDt and defaults GrpHdr/CreDtTm.
struct Payment {
  std::optional<Address> from;  // required by builders; optional on inbound
  std::optional<Address> to;    // (non-Stellar accounts can't resolve)
  Asset asset = Asset::native();
  Amount amount;
  Memo memo = Memo::none();

  std::string id;       // caller-side payment reference (InstrId/TxId default)
  std::string tx_hash;  // hex ledger tx hash → EndToEndId
  std::int64_t created_at = 0;  // unix seconds; 0 → builder uses its clock

  bool operator==(const Payment&) const = default;
};

// ---------------------------------------------------------------------------
// Party — user-facing party detail
// ---------------------------------------------------------------------------

/// Debtor/creditor detail. Account precedence when mapped to XML:
/// `acct_iban` → `acct_id` (≤34 chars, goes to Othr/Id) → `acct_proxy_id`
/// or an over-long `acct_id` (goes to Prxy/Id — this is where 56-char
/// Stellar strkeys land, since Othr/Id is Max34Text).
struct Party {
  std::string name;
  std::optional<PostalAddress> address;
  std::string phone, email;
  std::string id;         // Othr/Id on the party's OrgId
  std::string id_scheme;  // Othr/SchmeNm/Cd
  std::string agent_bic;  // BICFI of the party's agent
  std::string acct_iban;
  std::string acct_id;
  std::string acct_proxy_id;
  std::string acct_proxy_type;  // e.g. "WALLET", "PHONE", "EMAIL"
  bool operator==(const Party&) const = default;
};

/// ISO transaction-level status codes (TxSts).
enum class TransactionStatus { kACSC, kACCP, kACSP, kRJCT, kPDNG };
[[nodiscard]] STELLAR_API std::string_view transaction_status_code(TransactionStatus s) noexcept;

/// A payment plus the party detail ISO 20022 needs. Missing party fields
/// fall back to Payment-derived defaults at build time. `tx_status`/
/// `tx_reason` are only read by status-report builders.
struct CreditTransferInstruction {
  Payment payment;
  std::optional<Party> debtor, creditor;
  std::vector<std::string> remittance_info;
  std::optional<TransactionStatus> tx_status;
  std::string tx_reason;
  bool operator==(const CreditTransferInstruction&) const = default;
};

// ---------------------------------------------------------------------------
// Builder options
// ---------------------------------------------------------------------------

/// Shared option fields for credit-transfer documents.
struct TransferOptions {
  std::string instg_bic, instd_bic;    // GrpHdr-level agents
  std::string dbtr_bic, cdtr_bic;      // party agents when Party::agent_bic unset
  std::string dbtr_name, cdtr_name;    // overrides (else truncated addresses)
  std::optional<PostalAddress> dbtr_address, cdtr_address;
  std::string dbtr_acct_iban, cdtr_acct_iban;
  std::string charge_bearer = "SHAR";  // DEBT|CRED|SHAR|SLEV
  std::string purpose_code;
  std::vector<std::string> remittance_info;
  std::string uetr, tx_id, instr_id;   // overrides; UETR defaults to a UUIDv4
  std::string msg_id;                  // GrpHdr/MsgId; default "<prefix><payment.id>"
  std::string msg_id_prefix = "SCPP";  // used when msg_id empty
};

struct TransferBatchOptions {
  TransferOptions tx;
  std::optional<bool> batch_booking;
  std::optional<Party> debtor;       // batch-level debtor default
  std::string cre_dt_tm;             // RFC3339; default = first payment's date
  std::string reqd_exctn_dt;         // pain.001 only; default = that date
  std::string initg_pty_name = "stellar-cpp-sdk";  // pain.001 InitgPty (mandatory)
};

/// Options for status reports (pacs.002 / pain.002).
struct StatusReportOptions {
  std::string msg_id;
  std::string orgnl_msg_id, orgnl_msg_nm_id, orgnl_cre_dt_tm;  // OrgnlGrpInfAndSts
  std::string sts_id;
};

/// Options for statements/notifications (camt.053 / camt.054).
struct StatementOptions {
  std::string msg_id;
  std::string stmt_id, ntfctn_id;
  std::string elctrnc_seq_nb;
  std::vector<Balance> balances;  // camt.053 only
};

// ---------------------------------------------------------------------------
// StatusReport + reconciliation
// ---------------------------------------------------------------------------

/// Flattened per-transaction status extracted from a pacs.002 or pain.002.
struct StatusReport {
  std::string sts_id;
  std::string end_to_end_id;  // OrgnlEndToEndId — matches the sent EndToEndId
  std::string instr_id;       // OrgnlInstrId
  std::string tx_id;          // OrgnlTxId
  std::string status;         // TxSts: "ACSC","RJCT","PDNG",…
  std::string reason_code;    // StsRsnInf>Rsn (Cd or Prtry)
  std::string reason_info;    // first AddtlInf line
  std::string original_msg_id;
  bool operator==(const StatusReport&) const = default;
};

struct ReconciliationResult {
  const CreditTransferInstruction* instruction = nullptr;
  std::string payment_id;
  std::optional<StatusReport> report;
  bool matched = false;
};

struct ReconciliationSummary {
  std::vector<ReconciliationResult> results;
  std::vector<StatusReport> unmatched;
};

/// Match status reports back to the instructions that were sent, keyed by
/// EndToEndId, then InstrId, then TxId (the identifiers the builders emit).
/// Last report for an instruction wins; reports referencing payments outside
/// the batch land in `unmatched`.
[[nodiscard]] STELLAR_API ReconciliationSummary reconcile_instructions(
    const std::vector<CreditTransferInstruction>& instructions,
    const std::vector<StatusReport>& reports);

}  // namespace stellar::inline v1::iso20022
