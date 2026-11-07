// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file documents.hpp
/// ISO 20022 document model (a pragmatic subset of each message, verified
/// against the vendored XSDs in iso20022/xsd/).
///
/// Field names keep the XML element names (camelCase) verbatim so the mapping
/// to the schema stays obvious; optional members map to minOccurs="0" and
/// vectors to unbounded elements. Comments note the XSD type and the required
/// elements of the implemented subset.

#include "stellar/export.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace stellar::inline v1::iso20022 {

// ---------------------------------------------------------------------------
// Shared building blocks
// ---------------------------------------------------------------------------

/// Amount with an ISO 4217 currency attribute (ActiveOrHistoricCurrencyAndAmount
/// and friends). `value` is a decimal string; emit truncates to the 5 fraction
/// digits the schema allows for settlement amounts.
struct CcyAmount {
  std::string ccy;
  std::string value;
  bool operator==(const CcyAmount&) const = default;
};

/// PostalAddress27 subset.
struct PostalAddress {
  std::string strt_nm, bldg_nb, bldg_nm, flr, pst_cd, twn_nm, twn_lctn_nm, dstrct_nm, ctry_sub_dvsn, ctry;
  std::vector<std::string> adr_line;
  bool operator==(const PostalAddress&) const = default;
};

/// Generic *Identification1: Othr{Id, SchmeNm{Cd|Prtry}, Issr}.
struct GenericId {
  std::string id;
  std::string schme_cd, schme_prtry, issr;
  bool operator==(const GenericId&) const = default;
};

/// Contact13 subset.
struct ContactDetails {
  std::string nm, phne_nb, mob_nb, email_adr;
  bool operator==(const ContactDetails&) const = default;
};

/// Party52Choice: party identified as an organisation or a private person.
struct PartyId {
  // OrganisationIdentification39 subset
  std::string any_bic, lei;
  std::vector<GenericId> org_othr;
  // PersonIdentification18 subset (private side)
  std::string birth_dt, city_of_birth, ctry_of_birth;
  std::vector<GenericId> prvt_othr;
  bool operator==(const PartyId&) const = default;
};

/// PartyIdentification272 subset: Nm?, PstlAdr?, Id?, CtryOfRes?, CtctDtls?
struct PartyIdentification {
  std::string nm;
  std::optional<PostalAddress> pstl_adr;
  std::optional<PartyId> id;
  std::string ctry_of_res;
  std::optional<ContactDetails> ctct_dtls;
  bool operator==(const PartyIdentification&) const = default;
};

/// ClearingSystemMemberIdentification2 subset.
struct ClearingSystemMember {
  std::string clr_sys_cd, clr_sys_prtry, mmb_id;
  bool operator==(const ClearingSystemMember&) const = default;
};

/// BranchAndFinancialInstitutionIdentification8 / FinancialInstitutionIdentification23
/// subset: BICFI?, ClrSysMmbId?, LEI?, Nm?, PstlAdr?, Othr?
struct Agent {
  std::string bicfi;
  std::optional<ClearingSystemMember> clr_sys_mmb_id;
  std::string lei, nm;
  std::optional<PostalAddress> pstl_adr;
  std::optional<GenericId> othr;
  bool operator==(const Agent&) const = default;
};

/// AccountIdentification4Choice: IBAN or generic Othr.
struct AccountId {
  std::string iban;
  std::optional<GenericId> othr;
  bool operator==(const AccountId&) const = default;
};

/// ProxyAccountIdentification1: Tp?, Id.
struct ProxyAccount {
  std::string tp_prtry, id;
  bool operator==(const ProxyAccount&) const = default;
};

/// CashAccount40 subset: Id?, Tp?, Ccy?, Nm?, Prxy?
struct CashAccount {
  std::optional<AccountId> id;
  std::string tp_cd, tp_prtry, ccy, nm;
  std::optional<ProxyAccount> prxy;
  bool operator==(const CashAccount&) const = default;
};

/// PaymentIdentification13 (pacs): InstrId?, EndToEndId, TxId?, UETR?, ClrSysRef?
struct PaymentIdentification {
  std::string instr_id;
  std::string end_to_end_id;  // required
  std::string tx_id, uetr, clr_sys_ref;
  bool operator==(const PaymentIdentification&) const = default;
};

/// PaymentIdentification6 (pain.001): InstrId?, EndToEndId, UETR?
struct PaymentIdentification6 {
  std::string instr_id;
  std::string end_to_end_id;  // required
  std::string uetr;
  bool operator==(const PaymentIdentification6&) const = default;
};

/// Purpose2Choice: Cd | Prtry.
struct Purpose {
  std::string cd, prtry;
  bool operator==(const Purpose&) const = default;
};

/// ReferredDocumentInformation subset: RfrdDocInf/Nb.
struct StructuredRemittance {
  std::string rfrd_doc_nb;
  bool operator==(const StructuredRemittance&) const = default;
};

/// RemittanceInformation26: Ustrd*, Strd*.
struct RemittanceInfo {
  std::vector<std::string> ustrd;
  std::vector<StructuredRemittance> strd;
  bool operator==(const RemittanceInfo&) const = default;
};

/// Charges16 subset: Amt, Agt, Tp?
struct ChargeInfo {
  CcyAmount amt;
  Agent agt;
  std::string tp_cd, tp_prtry;
  bool operator==(const ChargeInfo&) const = default;
};

/// Proprietary asset identification emitted inside
/// SplmtryData/Envlp/Asset — preserves a non-ISO-4217 Stellar asset
/// (code, issuer, exact amount) when Ccy is forced to "XXX".
struct SupplementaryAsset {
  std::string cd, issr, amt;
  bool operator==(const SupplementaryAsset&) const = default;
};

/// SettlementInstruction15 subset: SttlmMtd.
struct SettlementInstruction {
  std::string sttlm_mtd;
  bool operator==(const SettlementInstruction&) const = default;
};

/// GroupHeader superset across the implemented messages. The per-message
/// emitters write only the members their version requires; e.g. pacs.008
/// needs SttlmInf, pain.001 needs InitgPty, pacs.002/camt emit neither.
struct GroupHeader {
  std::string msg_id, cre_dt_tm;  // required everywhere
  std::optional<bool> btch_bookg;
  std::string nb_of_txs, ctrl_sum;                 // pacs.008/pain.001/pacs.004
  std::optional<CcyAmount> ttl_intrbk_sttlm_amt;   // pacs.008
  std::string intrbk_sttlm_dt;                     // pacs.008
  std::optional<SettlementInstruction> sttlm_inf;  // required in pacs.008
  std::optional<PartyIdentification> initg_pty;    // required in pain.001
  std::optional<Agent> instg_agt, instd_agt;
  bool operator==(const GroupHeader&) const = default;
};

/// PaymentTypeInformation28 subset.
struct PaymentTypeInfo {
  std::string instr_prty, clr_chanl;
  std::string svc_lvl_cd, svc_lvl_prtry;
  std::string lcl_instrm_cd, lcl_instrm_prtry;
  std::string ctgy_purp_cd, ctgy_purp_prtry;
  bool operator==(const PaymentTypeInfo&) const = default;
};

// ---------------------------------------------------------------------------
// pacs.008.001.14 — FIToFICstmrCdtTrf
// ---------------------------------------------------------------------------

/// CreditTransferTransaction73 subset — element order follows the XSD sequence.
struct CreditTransferTx {
  PaymentIdentification pmt_id;
  std::optional<PaymentTypeInfo> pmt_tp_inf;
  CcyAmount intrbk_sttlm_amt;
  std::string intrbk_sttlm_dt;
  std::optional<CcyAmount> instd_amt;
  std::string chrg_br;  // required
  std::vector<ChargeInfo> chrgs_inf;
  std::optional<Agent> instg_agt, instd_agt;
  std::optional<PartyIdentification> ultmt_dbtr, initg_pty;
  PartyIdentification dbtr;  // required
  std::optional<CashAccount> dbtr_acct;
  Agent dbtr_agt;  // required
  std::optional<CashAccount> dbtr_agt_acct;
  Agent cdtr_agt;  // required
  std::optional<CashAccount> cdtr_agt_acct;
  PartyIdentification cdtr;  // required
  std::optional<CashAccount> cdtr_acct;
  std::optional<PartyIdentification> ultmt_cdtr;
  std::optional<Purpose> purp;
  std::optional<RemittanceInfo> rmt_inf;
  std::vector<SupplementaryAsset> splmtry_assets;
  bool operator==(const CreditTransferTx&) const = default;
};

struct Pacs008 {
  GroupHeader grp_hdr;
  std::vector<CreditTransferTx> txs;
  bool operator==(const Pacs008&) const = default;
};

// ---------------------------------------------------------------------------
// pacs.002.001.16 — FIToFIPmtStsRpt
// ---------------------------------------------------------------------------

/// OriginalGroupInformation33 / OriginalGroupHeader22 subset.
struct OriginalGroupInfo {
  std::string orgnl_msg_id, orgnl_msg_nm_id, orgnl_cre_dt_tm;
  bool operator==(const OriginalGroupInfo&) const = default;
};

/// StatusReasonInformation14: Orgtr?, Rsn?, AddtlInf*.
struct StatusReasonInfo {
  std::optional<PartyIdentification> orgtr;
  std::string rsn_cd, rsn_prtry;
  std::vector<std::string> addtl_inf;
  bool operator==(const StatusReasonInfo&) const = default;
};

/// OriginalTransactionReference45/47 subset.
struct OriginalTxRef {
  std::optional<CcyAmount> intrbk_sttlm_amt;
  std::string intrbk_sttlm_dt;
  std::optional<RemittanceInfo> rmt_inf;
  std::optional<PartyIdentification> ultmt_dbtr, dbtr;
  std::optional<CashAccount> dbtr_acct;
  std::optional<Agent> dbtr_agt;
  std::optional<Agent> cdtr_agt;
  std::optional<PartyIdentification> cdtr;
  std::optional<CashAccount> cdtr_acct;
  std::optional<PartyIdentification> ultmt_cdtr;
  std::optional<Purpose> purp;
  bool operator==(const OriginalTxRef&) const = default;
};

/// PaymentTransaction177 (TxInfAndSts) subset.
struct PaymentTxStatus {
  std::string sts_id;
  std::optional<OriginalGroupInfo> orgnl_grp_inf;
  std::string orgnl_instr_id, orgnl_end_to_end_id, orgnl_tx_id, orgnl_uetr;
  std::string tx_sts;
  std::vector<StatusReasonInfo> sts_rsn_inf;
  std::string acct_svcr_ref, clr_sys_ref;
  std::optional<OriginalTxRef> orgnl_tx_ref;
  bool operator==(const PaymentTxStatus&) const = default;
};

struct Pacs002 {
  GroupHeader grp_hdr;
  std::vector<OriginalGroupInfo> orgnl_grp_inf_and_sts;
  std::vector<PaymentTxStatus> txs;
  bool operator==(const Pacs002&) const = default;
};

// ---------------------------------------------------------------------------
// pacs.004.001.15 — PmtRtr
// ---------------------------------------------------------------------------

/// PaymentReturnReason7: Orgtr?, Rsn?, AddtlInf*.
struct ReturnReasonInfo {
  std::optional<PartyIdentification> orgtr;
  std::string rsn_cd, rsn_prtry;
  std::vector<std::string> addtl_inf;
  bool operator==(const ReturnReasonInfo&) const = default;
};

/// PaymentTransaction168 (TxInf) subset.
struct PaymentReturnTx {
  std::string rtr_id;
  std::optional<OriginalGroupInfo> orgnl_grp_inf;
  std::string orgnl_instr_id, orgnl_end_to_end_id, orgnl_tx_id, orgnl_uetr;
  CcyAmount rtrd_intrbk_sttlm_amt;  // required
  std::string intrbk_sttlm_dt;
  std::string chrg_br;
  std::vector<ReturnReasonInfo> rtr_rsn_inf;
  std::optional<OriginalTxRef> orgnl_tx_ref;
  bool operator==(const PaymentReturnTx&) const = default;
};

struct Pacs004 {
  GroupHeader grp_hdr;
  std::optional<OriginalGroupInfo> orgnl_grp_inf;
  std::vector<PaymentReturnTx> txs;
  bool operator==(const Pacs004&) const = default;
};

// ---------------------------------------------------------------------------
// pain.001.001.13 — CstmrCdtTrfInitn
// ---------------------------------------------------------------------------

/// CreditTransferTransaction76 (per-PmtInf CdtTrfTxInf) subset.
struct PainTransferTx {
  PaymentIdentification6 pmt_id;
  std::optional<PaymentTypeInfo> pmt_tp_inf;
  CcyAmount instd_amt;  // required (Amt choice: InstdAmt)
  std::string chrg_br;
  std::optional<PartyIdentification> ultmt_dbtr;
  std::optional<Agent> cdtr_agt;
  std::optional<CashAccount> cdtr_agt_acct;
  std::optional<PartyIdentification> cdtr;
  std::optional<CashAccount> cdtr_acct;
  std::optional<PartyIdentification> ultmt_cdtr;
  std::optional<Purpose> purp;
  std::optional<RemittanceInfo> rmt_inf;
  std::vector<SupplementaryAsset> splmtry_assets;
  bool operator==(const PainTransferTx&) const = default;
};

/// PaymentInstruction51 subset. PmtInfId, PmtMtd, ReqdExctnDt, Dbtr,
/// DbtrAcct, DbtrAgt are required by the schema.
struct PaymentInstruction {
  std::string pmt_inf_id;
  std::string pmt_mtd = "TRF";
  std::optional<bool> btch_bookg;
  std::string nb_of_txs, ctrl_sum;
  std::string reqd_exctn_dt;  // required
  PartyIdentification dbtr;
  CashAccount dbtr_acct;
  Agent dbtr_agt;
  std::string chrg_br;
  std::vector<PainTransferTx> txs;
  bool operator==(const PaymentInstruction&) const = default;
};

struct Pain001 {
  GroupHeader grp_hdr;  // InitgPty is required in pain.001 GroupHeader114
  std::vector<PaymentInstruction> pmts;
  bool operator==(const Pain001&) const = default;
};

// ---------------------------------------------------------------------------
// pain.002.001.15 — CstmrPmtStsRpt
// ---------------------------------------------------------------------------

/// OriginalPaymentInstruction subset: OrgnlPmtInfId + TxInfAndSts.
struct OriginalPaymentInstruction {
  std::string orgnl_pmt_inf_id;
  std::vector<PaymentTxStatus> txs;
  bool operator==(const OriginalPaymentInstruction&) const = default;
};

struct Pain002 {
  GroupHeader grp_hdr;
  std::optional<OriginalGroupInfo> orgnl_grp_inf_and_sts;
  std::vector<OriginalPaymentInstruction> pmts;
  bool operator==(const Pain002&) const = default;
};

// ---------------------------------------------------------------------------
// camt.053.001.14 / camt.054.001.14 — BkToCstmrStmt / BkToCstmrDbtCdtNtfctn
// ---------------------------------------------------------------------------

/// TransactionReferences10 subset.
struct EntryRefs {
  std::string msg_id, acct_svcr_ref, pmt_inf_id, instr_id, end_to_end_id, uetr, tx_id, rtr_id, clr_sys_ref;
  bool operator==(const EntryRefs&) const = default;
};

/// TransactionParties12 subset.
struct EntryParties {
  std::optional<PartyIdentification> initg_pty, dbtr, ultmt_dbtr, cdtr, ultmt_cdtr;
  std::optional<CashAccount> dbtr_acct, cdtr_acct;
  bool operator==(const EntryParties&) const = default;
};

/// EntryTransaction16 (TxDtls) subset.
struct EntryTxDetails {
  std::optional<EntryRefs> refs;
  std::optional<CcyAmount> amt;
  std::string cdt_dbt_ind;
  std::optional<EntryParties> rltd_pties;
  std::optional<RemittanceInfo> rmt_inf;
  std::string addtl_tx_inf;
  std::vector<SupplementaryAsset> splmtry_assets;
  bool operator==(const EntryTxDetails&) const = default;
};

/// ReportEntry16 (Ntry) subset. BkTxCd is schema-required; we emit a minimal
/// Prtry code when the caller doesn't supply one.
struct Entry {
  std::string ntry_ref;
  CcyAmount amt;          // required
  std::string cdt_dbt_ind;  // required: "CRDT" | "DBIT"
  std::string rvsl_ind;   // "true"/"false"
  std::string sts;        // required: e.g. "BOOK"
  std::string bookg_dt, val_dt;  // ISO dates or datetimes
  std::string acct_svcr_ref;
  std::string bk_tx_cd_prtry, bk_tx_cd_issr;
  std::vector<EntryTxDetails> tx_dtls;
  std::string addtl_ntry_inf;
  bool operator==(const Entry&) const = default;
};

/// CashBalance8 subset: Tp, Amt, CdtDbtInd, Dt.
struct Balance {
  std::string tp_cd, tp_prtry;  // Tp choice
  CcyAmount amt;
  std::string cdt_dbt_ind;
  std::string dt;
  bool operator==(const Balance&) const = default;
};

/// AccountStatement15 (Stmt) subset.
struct Statement {
  std::string id;
  std::string elctrnc_seq_nb;
  std::string cre_dt_tm;
  CashAccount acct;
  std::optional<Agent> ownr_agt;  // not in Stmt; kept for Ntfctn parity
  std::vector<Balance> bals;
  std::vector<Entry> ntrys;
  std::string addtl_stmt_inf;
  bool operator==(const Statement&) const = default;
};

struct Camt053 {
  GroupHeader grp_hdr;
  std::vector<Statement> stmts;
  bool operator==(const Camt053&) const = default;
};

/// AccountNotification25 (Ntfctn) subset — same shape as Stmt minus Bal.
struct Notification {
  std::string id;
  std::string elctrnc_seq_nb;
  std::string cre_dt_tm;
  CashAccount acct;
  std::vector<Entry> ntrys;
  std::string addtl_ntfctn_inf;
  bool operator==(const Notification&) const = default;
};

struct Camt054 {
  GroupHeader grp_hdr;
  std::vector<Notification> ntfctns;
  bool operator==(const Camt054&) const = default;
};

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------

enum class MessageKind {
  kPacs008, kPacs002, kPacs004, kPain001, kPain002, kCamt053, kCamt054,
};

using Document = std::variant<Pacs008, Pacs002, Pacs004, Pain001, Pain002, Camt053, Camt054>;

/// Message identifier ("pacs.008.001.14") ↔ MessageKind, and the
/// `urn:iso:std:iso:20022:tech:xsd:*` namespace emitted/accepted.
[[nodiscard]] STELLAR_API MessageKind message_kind(const Document& d) noexcept;
[[nodiscard]] STELLAR_API std::string_view message_id(MessageKind k) noexcept;
[[nodiscard]] STELLAR_API std::string_view message_namespace(MessageKind k) noexcept;

}  // namespace stellar::inline v1::iso20022
