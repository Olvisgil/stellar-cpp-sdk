// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file messages.hpp (private)
/// Per-message emit/read entry points plus the shared emit/read helpers for
/// the building blocks (parties, agents, accounts, remittance, supplementary
/// data) that appear in several messages.

#include "stellar/iso20022/documents.hpp"
#include "stellar/iso20022/payment.hpp"
#include "stellar/result.hpp"
#include "xml_util.hpp"

#include <pugixml.hpp>

namespace stellar::inline v1::iso20022::detail {

// Per-message emitters: write the message element (e.g. FIToFICstmrCdtTrf)
// inside the <Document> node `doc`.
void emit_pacs008(pugi::xml_node doc, const Pacs008& m);
void emit_pacs002(pugi::xml_node doc, const Pacs002& m);
void emit_pacs004(pugi::xml_node doc, const Pacs004& m);
void emit_pain001(pugi::xml_node doc, const Pain001& m);
void emit_pain002(pugi::xml_node doc, const Pain002& m);
void emit_camt053(pugi::xml_node doc, const Camt053& m);
void emit_camt054(pugi::xml_node doc, const Camt054& m);

// Per-message readers: `doc` is the <Document> node whose xmlns was already
// validated. Errors carry field paths.
Result<Pacs008> read_pacs008(pugi::xml_node doc);
Result<Pacs002> read_pacs002(pugi::xml_node doc);
Result<Pacs004> read_pacs004(pugi::xml_node doc);
Result<Pain001> read_pain001(pugi::xml_node doc);
Result<Pain002> read_pain002(pugi::xml_node doc);
Result<Camt053> read_camt053(pugi::xml_node doc);
Result<Camt054> read_camt054(pugi::xml_node doc);

// ---------------------------------------------------------------------------
// Shared building blocks (implemented in common.cpp)
// ---------------------------------------------------------------------------

void emit_postal_address(pugi::xml_node parent, const char* name, const PostalAddress& a);
void emit_generic_id(pugi::xml_node parent, const char* name, const GenericId& id);
void emit_contact(pugi::xml_node parent, const char* name, const ContactDetails& c);
void emit_party(pugi::xml_node parent, const char* name, const PartyIdentification& p);
/// Party50Choice contexts (OrgnlTxRef, RltdPties): <name><Pty>…</Pty></name>.
void emit_party50(pugi::xml_node parent, const char* name, const PartyIdentification& p);
void emit_agent(pugi::xml_node parent, const char* name, const Agent& a);
void emit_cash_account(pugi::xml_node parent, const char* name, const CashAccount& a);
void emit_pmt_id(pugi::xml_node parent, const PaymentIdentification& id);
void emit_pmt_id6(pugi::xml_node parent, const PaymentIdentification6& id);
void emit_purpose(pugi::xml_node parent, const Purpose& p);
void emit_rmt_inf(pugi::xml_node parent, const RemittanceInfo& r);
void emit_splmtry_assets(pugi::xml_node tx, const std::vector<SupplementaryAsset>& assets);
void emit_amount(pugi::xml_node parent, const char* name, const CcyAmount& a);
void emit_amount_if(pugi::xml_node parent, const char* name, const std::optional<CcyAmount>& a);
void emit_orgnl_grp_inf(pugi::xml_node parent, const char* name, const OriginalGroupInfo& g);
void emit_orgnl_tx_ref(pugi::xml_node parent, const OriginalTxRef& r);
void emit_entry_tx(pugi::xml_node parent, const Entry& e);            // shared camt Ntry shape
void emit_grp_hdr_base(pugi::xml_node parent, const GroupHeader& h);  // MsgId/CreDtTm prefix

// TxInfAndSts shared by pacs.002 and pain.002 (defined in pacs002.cpp).
void emit_pacs002_tx_sts(pugi::xml_node parent, const PaymentTxStatus& t);
void emit_pain002_tx_sts(pugi::xml_node parent, const PaymentTxStatus& t);  // no OrgnlTxId
void read_pacs002_tx_sts(pugi::xml_node n, PaymentTxStatus& t);

void read_postal_address(pugi::xml_node n, PostalAddress& a);
void read_generic_id(pugi::xml_node n, GenericId& id);
void read_contact(pugi::xml_node n, ContactDetails& c);
void read_party(pugi::xml_node n, PartyIdentification& p);
void read_party50(pugi::xml_node n, PartyIdentification& p);
void read_agent(pugi::xml_node n, Agent& a);
void read_cash_account(pugi::xml_node n, CashAccount& a);
void read_pmt_id(pugi::xml_node n, PaymentIdentification& id, const std::string& path,
                 std::vector<std::string>& errs);
void read_rmt_inf(pugi::xml_node n, RemittanceInfo& r);
std::vector<SupplementaryAsset> read_splmtry_assets(pugi::xml_node tx);
CcyAmount read_amount(pugi::xml_node n);
void read_orgnl_grp_inf(pugi::xml_node n, OriginalGroupInfo& g, const std::string& path,
                        std::vector<std::string>& errs);
void read_orgnl_tx_ref(pugi::xml_node n, OriginalTxRef& r);
void read_entry_tx(pugi::xml_node parent, Entry& e);  // shared camt Ntry shape
void read_grp_hdr_base(pugi::xml_node parent, GroupHeader& h, const std::string& path,
                       std::vector<std::string>& errs);

// ---------------------------------------------------------------------------
// Small shared helpers (implemented in common.cpp)
// ---------------------------------------------------------------------------

[[nodiscard]] std::string safe_truncate(std::string_view s, std::size_t n);
[[nodiscard]] std::string normalize_amount5(std::string_view amt);  // truncate to 5dp
[[nodiscard]] Result<std::string> decimal_sum(const std::vector<std::string>& amounts);
[[nodiscard]] std::string uuid_v4(RandomSource& rng);
[[nodiscard]] std::string format_isodate(std::int64_t unix_seconds);
[[nodiscard]] std::string format_isodatetime(std::int64_t unix_seconds);

/// Settlement currency for an asset code(+issuer): ISO 4217 codes pass
/// through as Ccy; anything else becomes "XXX" plus a SplmtryData asset.
[[nodiscard]] std::pair<std::string, std::optional<SupplementaryAsset>> settlement_currency(
    std::string_view asset_code, std::string_view asset_issuer, std::string_view exact_amount);

[[nodiscard]] Agent agent_by_bic(std::string_view bic);
[[nodiscard]] Agent agent_by_other_id(std::string_view id);
[[nodiscard]] Agent agent_or_fallback(std::string_view bic, std::string_view fallback_id);

/// Party domain type → XSD structs (account precedence IBAN→Othr→Prxy).
[[nodiscard]] PartyIdentification party_identification(const Party* p,
                                                       std::string_view fallback_nm);
[[nodiscard]] std::optional<CashAccount> party_account(const Party* p);
[[nodiscard]] RemittanceInfo remittance_info(const std::vector<std::string>& lines,
                                             const Payment& p);
[[nodiscard]] std::string end_to_end_id(const Payment& p);
[[nodiscard]] std::string payment_asset(const Payment& p);

[[nodiscard]] Error join_errors(std::vector<std::string> errs);

}  // namespace stellar::inline v1::iso20022::detail
