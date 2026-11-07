// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/iso20022/stellar_map.hpp"

#include "detail/messages.hpp"
#include "stellar/iso20022/xml.hpp"

#include <charconv>
#include <unordered_map>

namespace stellar::inline v1::iso20022 {

const Clock& default_clock() {
  static const SystemClock c;
  return c;
}

std::string_view transaction_status_code(TransactionStatus s) noexcept {
  switch (s) {
    case TransactionStatus::kACSC: return "ACSC";
    case TransactionStatus::kACCP: return "ACCP";
    case TransactionStatus::kACSP: return "ACSP";
    case TransactionStatus::kRJCT: return "RJCT";
    case TransactionStatus::kPDNG: return "PDNG";
  }
  return {};
}

namespace {

RandomSource& rng() {
  static SecureRandom r;
  return r;
}

/// CashAccount whose Prxy/Id carries a Stellar strkey — the account form our
/// emitted documents use for on-chain parties (Othr/Id is Max34Text; strkeys
/// are 56).
CashAccount stellar_acct(const Address& a) {
  CashAccount acct;
  acct.prxy = ProxyAccount{"WALLET", a.to_string()};
  return acct;
}

std::string acct_id_string(const CashAccount& a) {
  if (a.prxy && !a.prxy->id.empty()) return a.prxy->id;
  if (a.id) {
    if (!a.id->iban.empty()) return a.id->iban;
    if (a.id->othr && !a.id->othr->id.empty()) return a.id->othr->id;
  }
  return {};
}

/// Recover a Stellar address from an account element: Prxy/Id first (where
/// our emit puts strkeys), then Othr/Id. Non-strkey ids yield nullopt.
std::optional<Address> acct_address(const CashAccount* a) {
  if (!a) return std::nullopt;
  const auto id = acct_id_string(*a);
  if (id.empty()) return std::nullopt;
  if (auto r = Address::parse(id); r.ok()) return *r;
  return std::nullopt;
}

/// Recover a Stellar asset from the settlement ccy + SplmtryData asset:
/// "XXX" → look up Envlp/Asset{Cd,Issr} ("XLM" or empty issuer → native,
/// else CODE:ISSUER); ISO ccy → credit asset only if an issuer strkey is
/// available, otherwise the caller keeps just the ccy string.
void recover_asset(const std::string& ccy, const std::vector<SupplementaryAsset>& suppl,
                   CreditTransferInstruction& instr) {
  std::string code = ccy;
  std::string issuer;
  if (ccy == "XXX" && !suppl.empty()) {
    code = suppl.front().cd;
    issuer = suppl.front().issr;
    if (!suppl.front().amt.empty()) {
      if (auto amt = Amount::parse(suppl.front().amt); amt.ok()) {
        instr.payment.amount = *amt;
      }
    }
  }
  if (code == "XLM" || code.empty()) {
    instr.payment.asset = Asset::native();
    return;
  }
  if (!issuer.empty()) {
    if (auto a = Asset::credit(code, issuer); a.ok()) instr.payment.asset = *a;
  }
}

/// Remittance → Payment memo + caller remittance lines. Ustrd lines go to
/// remittance_info (single line also becomes a text memo); Strd/RfrdDocInf/Nb
/// holds a memo id (decimal) or hash/return ref from the outbound mapping.
void recover_remittance(const RemittanceInfo& rmt, CreditTransferInstruction& in, Payment& p) {
  in.remittance_info = rmt.ustrd;
  if (in.remittance_info.empty() && !rmt.strd.empty()) {
    const auto& nb = rmt.strd.front().rfrd_doc_nb;
    std::uint64_t id = 0;
    const auto [ptr, ec] = std::from_chars(nb.data(), nb.data() + nb.size(), id);
    if (ec == std::errc{} && ptr == nb.data() + nb.size()) p.memo = Memo::id(id);
  } else if (in.remittance_info.size() == 1) {
    p.memo = Memo::text(in.remittance_info.front());
  }
}

Result<void> require_parties(const Payment& p, const std::string& what) {
  if (!p.from || !p.to) {
    return Error::invalid_argument(what +
                                   ": payment requires from/to Stellar addresses");
  }
  return {};
}

/// Shared debtor/creditor resolution for the transfer builders.
struct ResolvedParties {
  const Party* debtor = nullptr;
  const Party* creditor = nullptr;
};

ResolvedParties resolve_parties(const CreditTransferInstruction& in,
                                const TransferBatchOptions& opts) {
  ResolvedParties r;
  r.debtor = in.debtor ? &*in.debtor : (opts.debtor ? &*opts.debtor : nullptr);
  r.creditor = in.creditor ? &*in.creditor : nullptr;
  return r;
}

CreditTransferTx pacs008_tx(const CreditTransferInstruction& in,
                            const TransferBatchOptions& opts, std::string_view tx_id,
                            std::string_view uetr, std::string_view sttlm_dt) {
  const Payment& p = in.payment;
  const auto parties = resolve_parties(in, opts);
  const TransferOptions& o = opts.tx;

  const auto [ccy, suppl] =
      detail::settlement_currency(detail::payment_asset(p), p.asset.issuer(), p.amount.to_string());

  CreditTransferTx t;
  t.pmt_id.instr_id = o.instr_id.empty() ? detail::safe_truncate(p.id, 35) : o.instr_id;
  t.pmt_id.end_to_end_id = detail::end_to_end_id(p);
  t.pmt_id.tx_id = o.tx_id.empty() ? std::string(tx_id) : o.tx_id;
  t.pmt_id.uetr = o.uetr.empty() ? std::string(uetr) : o.uetr;
  t.intrbk_sttlm_amt = {ccy, detail::normalize_amount5(p.amount.to_string())};
  t.intrbk_sttlm_dt = std::string(sttlm_dt);
  if (suppl) t.splmtry_assets.push_back(*suppl);
  t.chrg_br = o.charge_bearer;

  t.initg_pty = PartyIdentification{};
  t.initg_pty->nm = opts.initg_pty_name;

  const std::string dbtr_nm =
      !o.dbtr_name.empty() ? o.dbtr_name : detail::safe_truncate(p.from->to_string(), 16);
  t.dbtr = detail::party_identification(parties.debtor, dbtr_nm);
  if (o.dbtr_address && (!parties.debtor || !parties.debtor->address)) {
    t.dbtr.pstl_adr = o.dbtr_address;
  }
  if (auto acct = detail::party_account(parties.debtor)) {
    t.dbtr_acct = std::move(acct);
  } else if (!o.dbtr_acct_iban.empty()) {
    t.dbtr_acct = CashAccount{};
    t.dbtr_acct->id = AccountId{o.dbtr_acct_iban, std::nullopt};
  } else {
    t.dbtr_acct = stellar_acct(*p.from);
  }
  t.dbtr_agt = parties.debtor && !parties.debtor->agent_bic.empty()
                   ? detail::agent_by_bic(parties.debtor->agent_bic)
                   : detail::agent_or_fallback(o.dbtr_bic,
                                               detail::safe_truncate(p.from->to_string(), 35));

  const std::string cdtr_nm =
      !o.cdtr_name.empty() ? o.cdtr_name : detail::safe_truncate(p.to->to_string(), 16);
  t.cdtr = detail::party_identification(parties.creditor, cdtr_nm);
  if (o.cdtr_address && (!parties.creditor || !parties.creditor->address)) {
    t.cdtr.pstl_adr = o.cdtr_address;
  }
  t.cdtr_agt = parties.creditor && !parties.creditor->agent_bic.empty()
                   ? detail::agent_by_bic(parties.creditor->agent_bic)
                   : detail::agent_or_fallback(o.cdtr_bic,
                                               detail::safe_truncate(p.to->to_string(), 35));
  if (auto acct = detail::party_account(parties.creditor)) {
    t.cdtr_acct = std::move(acct);
  } else if (!o.cdtr_acct_iban.empty()) {
    t.cdtr_acct = CashAccount{};
    t.cdtr_acct->id = AccountId{o.cdtr_acct_iban, std::nullopt};
  } else {
    t.cdtr_acct = stellar_acct(*p.to);
  }
  if (!o.purpose_code.empty()) t.purp = Purpose{o.purpose_code, {}};

  const auto& lines = !in.remittance_info.empty() ? in.remittance_info : o.remittance_info;
  auto rmt = detail::remittance_info(lines, p);
  if (!rmt.ustrd.empty() || !rmt.strd.empty()) t.rmt_inf = std::move(rmt);
  return t;
}

Result<PaymentTxStatus> status_tx(const CreditTransferInstruction& in,
                                  const StatusReportOptions& opts, std::size_t index) {
  const Payment& p = in.payment;
  PaymentTxStatus t;
  t.sts_id = opts.sts_id.empty() ? "STS" + std::to_string(index + 1) : opts.sts_id;
  t.orgnl_instr_id = detail::safe_truncate(p.id, 35);
  t.orgnl_end_to_end_id = detail::end_to_end_id(p);
  t.orgnl_tx_id = detail::safe_truncate(p.id, 35);
  t.tx_sts = std::string(transaction_status_code(
      in.tx_status.value_or(TransactionStatus::kACSC)));
  if (!in.tx_reason.empty()) {
    StatusReasonInfo r;
    r.rsn_prtry = detail::safe_truncate(in.tx_reason, 35);
    r.addtl_inf.push_back(in.tx_reason);
    t.sts_rsn_inf.push_back(std::move(r));
  }
  t.acct_svcr_ref = detail::safe_truncate(p.tx_hash, 35);
  return t;
}

Entry camt_entry(const CreditTransferInstruction& in, const CashAccount& acct) {
  const Payment& p = in.payment;
  const auto [ccy, suppl] =
      detail::settlement_currency(detail::payment_asset(p), p.asset.issuer(), p.amount.to_string());

  Entry e;
  e.ntry_ref = detail::safe_truncate(p.id, 35);
  e.amt = {ccy, detail::normalize_amount5(p.amount.to_string())};
  const auto owner = acct_id_string(acct);
  const bool inbound = p.to && p.to->to_string() == owner;
  e.cdt_dbt_ind = inbound ? "CRDT" : "DBIT";
  e.sts = "BOOK";
  const auto dt = detail::format_isodate(p.created_at);
  e.bookg_dt = dt;
  e.val_dt = dt;
  e.acct_svcr_ref = detail::safe_truncate(p.tx_hash, 35);
  e.bk_tx_cd_prtry = "PMNT";

  EntryTxDetails td;
  td.refs = EntryRefs{};
  td.refs->acct_svcr_ref = detail::safe_truncate(p.tx_hash, 35);
  td.refs->instr_id = detail::safe_truncate(p.id, 35);
  td.refs->end_to_end_id = detail::end_to_end_id(p);
  td.refs->tx_id = detail::safe_truncate(p.tx_hash.empty() ? p.id : p.tx_hash, 35);
  td.amt = e.amt;
  td.cdt_dbt_ind = e.cdt_dbt_ind;
  td.rltd_pties = EntryParties{};
  if (p.from) {
    td.rltd_pties->dbtr = PartyIdentification{};
    td.rltd_pties->dbtr->nm = detail::safe_truncate(p.from->to_string(), 140);
    td.rltd_pties->dbtr_acct = stellar_acct(*p.from);
  }
  if (p.to) {
    td.rltd_pties->cdtr = PartyIdentification{};
    td.rltd_pties->cdtr->nm = detail::safe_truncate(p.to->to_string(), 140);
    td.rltd_pties->cdtr_acct = stellar_acct(*p.to);
  }
  auto rmt = detail::remittance_info(in.remittance_info, p);
  if (!rmt.ustrd.empty() || !rmt.strd.empty()) td.rmt_inf = std::move(rmt);
  if (suppl) td.splmtry_assets.push_back(*suppl);
  e.tx_dtls.push_back(std::move(td));
  return e;
}

Result<void> require_instructions(std::span<const CreditTransferInstruction> instrs,
                                  std::string_view builder) {
  if (instrs.empty()) {
    return Error::invalid_argument(std::string(builder) + ": no instructions");
  }
  for (std::size_t i = 0; i < instrs.size(); ++i) {
    STELLAR_TRY(require_parties(instrs[i].payment,
                                std::string(builder) + ": instruction " + std::to_string(i)));
  }
  return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

Result<std::string> build_pacs008(const Payment& payment, const TransferOptions& opts,
                                  const Clock& clock) {
  CreditTransferInstruction in;
  in.payment = payment;
  in.remittance_info = opts.remittance_info;
  TransferBatchOptions b;
  b.tx = opts;
  const CreditTransferInstruction batch[] = {std::move(in)};
  return build_pacs008_batch(std::span<const CreditTransferInstruction>(batch, 1), b, clock);
}

Result<std::string> build_pacs008_batch(std::span<const CreditTransferInstruction> instrs,
                                        const TransferBatchOptions& opts, const Clock& clock) {
  STELLAR_TRY(require_instructions(instrs, "build_pacs008"));
  const Payment& first = instrs.front().payment;

  Pacs008 doc;
  const auto ts = first.created_at ? first.created_at : clock.now_seconds();
  doc.grp_hdr.msg_id = !opts.tx.msg_id.empty()
                           ? opts.tx.msg_id
                           : opts.tx.msg_id_prefix + detail::safe_truncate(first.id, 31);
  doc.grp_hdr.cre_dt_tm = opts.cre_dt_tm.empty() ? detail::format_isodatetime(ts) : opts.cre_dt_tm;
  doc.grp_hdr.btch_bookg = opts.batch_booking;
  doc.grp_hdr.nb_of_txs = std::to_string(instrs.size());
  doc.grp_hdr.sttlm_inf = SettlementInstruction{"CLRG"};
  doc.grp_hdr.intrbk_sttlm_dt = detail::format_isodate(ts);
  if (!opts.tx.instg_bic.empty()) doc.grp_hdr.instg_agt = detail::agent_by_bic(opts.tx.instg_bic);
  if (!opts.tx.instd_bic.empty()) doc.grp_hdr.instd_agt = detail::agent_by_bic(opts.tx.instd_bic);

  const auto sttlm_dt = doc.grp_hdr.intrbk_sttlm_dt;
  std::vector<std::string> amounts;
  amounts.reserve(instrs.size());
  std::string single_ccy;
  bool mixed_ccy = false;
  for (const auto& in : instrs) {
    const auto uetr = detail::uuid_v4(rng());
    const auto tx_id = detail::safe_truncate(in.payment.id, 35);
    doc.txs.push_back(pacs008_tx(in, opts, tx_id, uetr, sttlm_dt));
    const auto& amt = doc.txs.back().intrbk_sttlm_amt;
    amounts.push_back(amt.value);
    if (single_ccy.empty()) {
      single_ccy = amt.ccy;
    } else if (single_ccy != amt.ccy) {
      mixed_ccy = true;
    }
  }
  STELLAR_TRY_ASSIGN(doc.grp_hdr.ctrl_sum, detail::decimal_sum(amounts));
  if (!mixed_ccy) doc.grp_hdr.ttl_intrbk_sttlm_amt = CcyAmount{single_ccy, doc.grp_hdr.ctrl_sum};

  return to_xml(Document{std::move(doc)});
}

Result<std::string> build_pain001(std::span<const CreditTransferInstruction> instrs,
                                  const TransferBatchOptions& opts, const Clock& clock) {
  STELLAR_TRY(require_instructions(instrs, "build_pain001"));
  const Payment& first = instrs.front().payment;

  Pain001 doc;
  const auto ts = first.created_at ? first.created_at : clock.now_seconds();
  doc.grp_hdr.msg_id = !opts.tx.msg_id.empty()
                           ? opts.tx.msg_id
                           : opts.tx.msg_id_prefix + detail::safe_truncate(first.id, 31);
  doc.grp_hdr.cre_dt_tm = opts.cre_dt_tm.empty() ? detail::format_isodatetime(ts) : opts.cre_dt_tm;
  doc.grp_hdr.nb_of_txs = std::to_string(instrs.size());
  doc.grp_hdr.initg_pty = PartyIdentification{};
  doc.grp_hdr.initg_pty->nm = opts.initg_pty_name;

  PaymentInstruction pi;
  pi.pmt_inf_id = doc.grp_hdr.msg_id + "-PI1";
  pi.reqd_exctn_dt = opts.reqd_exctn_dt.empty() ? detail::format_isodate(ts) : opts.reqd_exctn_dt;
  pi.btch_bookg = opts.batch_booking;
  pi.nb_of_txs = doc.grp_hdr.nb_of_txs;
  pi.chrg_br = opts.tx.charge_bearer;

  const auto parties = resolve_parties(instrs.front(), opts);
  const TransferOptions& o = opts.tx;
  pi.dbtr = detail::party_identification(
      parties.debtor, !o.dbtr_name.empty() ? o.dbtr_name
                                         : detail::safe_truncate(first.from->to_string(), 140));
  if (auto acct = detail::party_account(parties.debtor)) {
    pi.dbtr_acct = std::move(*acct);
  } else if (!o.dbtr_acct_iban.empty()) {
    pi.dbtr_acct.id = AccountId{o.dbtr_acct_iban, std::nullopt};
  } else {
    pi.dbtr_acct = stellar_acct(*first.from);
  }
  pi.dbtr_agt = parties.debtor && !parties.debtor->agent_bic.empty()
                    ? detail::agent_by_bic(parties.debtor->agent_bic)
                    : detail::agent_or_fallback(o.dbtr_bic,
                                                detail::safe_truncate(first.from->to_string(), 35));

  std::vector<std::string> amounts;
  amounts.reserve(instrs.size());
  for (const auto& in : instrs) {
    const Payment& p = in.payment;
    const auto [ccy, suppl] =
        detail::settlement_currency(detail::payment_asset(p), p.asset.issuer(), p.amount.to_string());
    PainTransferTx t;
    t.pmt_id.instr_id = o.instr_id.empty() ? detail::safe_truncate(p.id, 35) : o.instr_id;
    t.pmt_id.end_to_end_id = detail::end_to_end_id(p);
    t.pmt_id.uetr = o.uetr.empty() ? detail::uuid_v4(rng()) : o.uetr;
    t.instd_amt = {ccy, detail::normalize_amount5(p.amount.to_string())};
    if (suppl) t.splmtry_assets.push_back(*suppl);
    const auto ip = resolve_parties(in, opts);
    if (ip.creditor && !ip.creditor->agent_bic.empty()) {
      t.cdtr_agt = detail::agent_by_bic(ip.creditor->agent_bic);
    } else if (!o.cdtr_bic.empty()) {
      t.cdtr_agt = detail::agent_by_bic(o.cdtr_bic);
    }
    const std::string cdtr_nm =
        !o.cdtr_name.empty() ? o.cdtr_name : detail::safe_truncate(p.to->to_string(), 140);
    if (auto c = detail::party_identification(ip.creditor, cdtr_nm); !c.nm.empty() || c.id) {
      t.cdtr = std::move(c);
    } else {
      PartyIdentification cdtr;
      cdtr.nm = cdtr_nm;
      t.cdtr = std::move(cdtr);
    }
    if (auto acct = detail::party_account(ip.creditor)) {
      t.cdtr_acct = std::move(acct);
    } else if (!o.cdtr_acct_iban.empty()) {
      CashAccount a;
      a.id = AccountId{o.cdtr_acct_iban, std::nullopt};
      t.cdtr_acct = std::move(a);
    } else {
      t.cdtr_acct = stellar_acct(*p.to);
    }
    if (!o.purpose_code.empty()) t.purp = Purpose{o.purpose_code, {}};
    const auto& lines = !in.remittance_info.empty() ? in.remittance_info : o.remittance_info;
    auto rmt = detail::remittance_info(lines, p);
    if (!rmt.ustrd.empty() || !rmt.strd.empty()) t.rmt_inf = std::move(rmt);
    amounts.push_back(t.instd_amt.value);
    pi.txs.push_back(std::move(t));
  }
  STELLAR_TRY_ASSIGN(pi.ctrl_sum, detail::decimal_sum(amounts));
  doc.grp_hdr.ctrl_sum = pi.ctrl_sum;
  doc.pmts.push_back(std::move(pi));
  return to_xml(Document{std::move(doc)});
}

Result<std::string> build_pacs002(std::span<const CreditTransferInstruction> instrs,
                                  const StatusReportOptions& opts, const Clock& clock) {
  if (instrs.empty()) return Error::invalid_argument("build_pacs002: no instructions");
  Pacs002 doc;
  const auto ts = instrs.front().payment.created_at ? instrs.front().payment.created_at
                                                  : clock.now_seconds();
  doc.grp_hdr.msg_id = opts.msg_id.empty() ? "STSRPT" + std::to_string(ts) : opts.msg_id;
  doc.grp_hdr.cre_dt_tm = detail::format_isodatetime(clock.now_seconds());
  if (!opts.orgnl_msg_id.empty()) {
    OriginalGroupInfo g;
    g.orgnl_msg_id = opts.orgnl_msg_id;
    g.orgnl_msg_nm_id = opts.orgnl_msg_nm_id.empty() ? "pacs.008.001.14" : opts.orgnl_msg_nm_id;
    g.orgnl_cre_dt_tm = opts.orgnl_cre_dt_tm;
    doc.orgnl_grp_inf_and_sts.push_back(std::move(g));
  }
  for (std::size_t i = 0; i < instrs.size(); ++i) {
    STELLAR_TRY_ASSIGN(auto t, status_tx(instrs[i], opts, i));
    doc.txs.push_back(std::move(t));
  }
  return to_xml(Document{std::move(doc)});
}

Result<std::string> build_pain002(std::span<const CreditTransferInstruction> instrs,
                                  const StatusReportOptions& opts, const Clock& clock) {
  if (instrs.empty()) return Error::invalid_argument("build_pain002: no instructions");
  Pain002 doc;
  const auto ts = instrs.front().payment.created_at ? instrs.front().payment.created_at
                                                  : clock.now_seconds();
  doc.grp_hdr.msg_id = opts.msg_id.empty() ? "CSTSRPT" + std::to_string(ts) : opts.msg_id;
  doc.grp_hdr.cre_dt_tm = detail::format_isodatetime(clock.now_seconds());
  if (!opts.orgnl_msg_id.empty()) {
    doc.orgnl_grp_inf_and_sts = OriginalGroupInfo{
        opts.orgnl_msg_id,
        opts.orgnl_msg_nm_id.empty() ? "pain.001.001.13" : opts.orgnl_msg_nm_id,
        opts.orgnl_cre_dt_tm};
  }
  OriginalPaymentInstruction op;
  op.orgnl_pmt_inf_id = doc.grp_hdr.msg_id + "-PI1";
  for (std::size_t i = 0; i < instrs.size(); ++i) {
    STELLAR_TRY_ASSIGN(auto t, status_tx(instrs[i], opts, i));
    op.txs.push_back(std::move(t));
  }
  doc.pmts.push_back(std::move(op));
  return to_xml(Document{std::move(doc)});
}

Result<std::string> build_pacs004(std::span<const CreditTransferInstruction> instrs,
                                  const TransferBatchOptions& opts, const Clock& clock) {
  STELLAR_TRY(require_instructions(instrs, "build_pacs004"));
  const Payment& first = instrs.front().payment;

  Pacs004 doc;
  const auto ts = first.created_at ? first.created_at : clock.now_seconds();
  doc.grp_hdr.msg_id = !opts.tx.msg_id.empty()
                           ? opts.tx.msg_id
                           : opts.tx.msg_id_prefix + "R" + detail::safe_truncate(first.id, 30);
  doc.grp_hdr.cre_dt_tm = opts.cre_dt_tm.empty() ? detail::format_isodatetime(ts) : opts.cre_dt_tm;
  doc.grp_hdr.nb_of_txs = std::to_string(instrs.size());
  doc.grp_hdr.sttlm_inf = SettlementInstruction{"CLRG"};
  doc.grp_hdr.intrbk_sttlm_dt = detail::format_isodate(ts);
  if (!opts.tx.instg_bic.empty()) doc.grp_hdr.instg_agt = detail::agent_by_bic(opts.tx.instg_bic);
  if (!opts.tx.instd_bic.empty()) doc.grp_hdr.instd_agt = detail::agent_by_bic(opts.tx.instd_bic);

  for (std::size_t i = 0; i < instrs.size(); ++i) {
    const auto& in = instrs[i];
    const Payment& p = in.payment;
    const auto [ccy, suppl] =
        detail::settlement_currency(detail::payment_asset(p), p.asset.issuer(), p.amount.to_string());
    PaymentReturnTx t;
    t.rtr_id = "RTR" + std::to_string(i + 1) + detail::safe_truncate(p.id, 29);
    t.orgnl_instr_id = detail::safe_truncate(p.id, 35);
    t.orgnl_end_to_end_id = detail::end_to_end_id(p);
    t.orgnl_tx_id = detail::safe_truncate(p.id, 35);
    t.rtrd_intrbk_sttlm_amt = {ccy, detail::normalize_amount5(p.amount.to_string())};
    t.intrbk_sttlm_dt = doc.grp_hdr.intrbk_sttlm_dt;
    t.chrg_br = opts.tx.charge_bearer;
    ReturnReasonInfo r;
    r.orgtr = PartyIdentification{};
    r.orgtr->nm = opts.initg_pty_name;
    r.rsn_prtry = in.tx_reason.empty() ? "RTRN" : detail::safe_truncate(in.tx_reason, 35);
    if (!in.tx_reason.empty()) r.addtl_inf.push_back(in.tx_reason);
    t.rtr_rsn_inf.push_back(std::move(r));
    // echo the original transaction
    OriginalTxRef ref;
    ref.intrbk_sttlm_amt = t.rtrd_intrbk_sttlm_amt;
    ref.intrbk_sttlm_dt = t.intrbk_sttlm_dt;
    const auto full = pacs008_tx(in, opts, t.orgnl_tx_id, "", t.intrbk_sttlm_dt);
    ref.dbtr = full.dbtr;
    ref.dbtr_acct = full.dbtr_acct;
    ref.dbtr_agt = full.dbtr_agt;
    ref.cdtr_agt = full.cdtr_agt;
    ref.cdtr = full.cdtr;
    ref.cdtr_acct = full.cdtr_acct;
    ref.rmt_inf = full.rmt_inf;
    t.orgnl_tx_ref = std::move(ref);
    doc.txs.push_back(std::move(t));
  }
  return to_xml(Document{std::move(doc)});
}

Result<std::string> build_camt053(const CashAccount& account,
                                  std::span<const CreditTransferInstruction> instrs,
                                  const StatementOptions& opts, const Clock& clock) {
  Camt053 doc;
  const auto ts = clock.now_seconds();
  doc.grp_hdr.msg_id = opts.msg_id.empty() ? "CAMT053" + std::to_string(ts) : opts.msg_id;
  doc.grp_hdr.cre_dt_tm = detail::format_isodatetime(ts);
  Statement s;
  s.id = opts.stmt_id.empty() ? doc.grp_hdr.msg_id + "-1" : opts.stmt_id;
  s.elctrnc_seq_nb = opts.elctrnc_seq_nb;
  s.cre_dt_tm = doc.grp_hdr.cre_dt_tm;
  s.acct = account;
  s.bals = opts.balances;
  for (const auto& in : instrs) s.ntrys.push_back(camt_entry(in, account));
  doc.stmts.push_back(std::move(s));
  return to_xml(Document{std::move(doc)});
}

Result<std::string> build_camt054(const CashAccount& account,
                                  std::span<const CreditTransferInstruction> instrs,
                                  const StatementOptions& opts, const Clock& clock) {
  Camt054 doc;
  const auto ts = clock.now_seconds();
  doc.grp_hdr.msg_id = opts.msg_id.empty() ? "CAMT054" + std::to_string(ts) : opts.msg_id;
  doc.grp_hdr.cre_dt_tm = detail::format_isodatetime(ts);
  Notification s;
  s.id = opts.ntfctn_id.empty() ? doc.grp_hdr.msg_id + "-1" : opts.ntfctn_id;
  s.elctrnc_seq_nb = opts.elctrnc_seq_nb;
  s.cre_dt_tm = doc.grp_hdr.cre_dt_tm;
  s.acct = account;
  for (const auto& in : instrs) s.ntrys.push_back(camt_entry(in, account));
  doc.ntfctns.push_back(std::move(s));
  return to_xml(Document{std::move(doc)});
}

// ---------------------------------------------------------------------------
// Readers
// ---------------------------------------------------------------------------

Result<std::vector<CreditTransferInstruction>> instructions_from_pacs008(const Pacs008& doc) {
  std::vector<CreditTransferInstruction> out;
  out.reserve(doc.txs.size());
  for (const auto& t : doc.txs) {
    CreditTransferInstruction in;
    Payment& p = in.payment;
    p.from = acct_address(t.dbtr_acct ? &*t.dbtr_acct : nullptr);
    p.to = acct_address(t.cdtr_acct ? &*t.cdtr_acct : nullptr);
    if (auto amt = Amount::parse(t.intrbk_sttlm_amt.value); amt.ok()) p.amount = *amt;
    p.id = !t.pmt_id.instr_id.empty() ? t.pmt_id.instr_id : t.pmt_id.tx_id;
    p.tx_hash = t.pmt_id.end_to_end_id;
    recover_asset(t.intrbk_sttlm_amt.ccy, t.splmtry_assets, in);
    if (t.rmt_inf) recover_remittance(*t.rmt_inf, in, p);
    if (!t.dbtr.nm.empty() || t.dbtr_acct) {
      in.debtor = Party{};
      in.debtor->name = t.dbtr.nm;
      in.debtor->address = t.dbtr.pstl_adr;
      if (t.dbtr_agt.bicfi.size() >= 8) in.debtor->agent_bic = t.dbtr_agt.bicfi;
      if (t.dbtr_acct) {
        in.debtor->acct_id = acct_id_string(*t.dbtr_acct);
      }
    }
    if (!t.cdtr.nm.empty() || t.cdtr_acct) {
      in.creditor = Party{};
      in.creditor->name = t.cdtr.nm;
      in.creditor->address = t.cdtr.pstl_adr;
      if (t.cdtr_agt.bicfi.size() >= 8) in.creditor->agent_bic = t.cdtr_agt.bicfi;
      if (t.cdtr_acct) {
        in.creditor->acct_id = acct_id_string(*t.cdtr_acct);
      }
    }
    out.push_back(std::move(in));
  }
  return out;
}

Result<std::vector<CreditTransferInstruction>> instructions_from_pain001(const Pain001& doc) {
  std::vector<CreditTransferInstruction> out;
  for (const auto& pi : doc.pmts) {
    for (const auto& t : pi.txs) {
      CreditTransferInstruction in;
      Payment& p = in.payment;
      p.to = acct_address(t.cdtr_acct ? &*t.cdtr_acct : nullptr);
      p.from = acct_address(&pi.dbtr_acct);
      if (auto amt = Amount::parse(t.instd_amt.value); amt.ok()) p.amount = *amt;
      p.id = t.pmt_id.instr_id;
      p.tx_hash = t.pmt_id.end_to_end_id;
      recover_asset(t.instd_amt.ccy, t.splmtry_assets, in);
      if (t.rmt_inf) recover_remittance(*t.rmt_inf, in, p);
      if (!pi.dbtr.nm.empty() || pi.dbtr_acct.id || pi.dbtr_acct.prxy) {
        in.debtor = Party{};
        in.debtor->name = pi.dbtr.nm;
        in.debtor->address = pi.dbtr.pstl_adr;
        in.debtor->agent_bic = pi.dbtr_agt.bicfi;
        in.debtor->acct_id = acct_id_string(pi.dbtr_acct);
      }
      if (t.cdtr && (!t.cdtr->nm.empty() || t.cdtr_acct)) {
        in.creditor = Party{};
        in.creditor->name = t.cdtr->nm;
        in.creditor->address = t.cdtr->pstl_adr;
        if (t.cdtr_agt) in.creditor->agent_bic = t.cdtr_agt->bicfi;
        if (t.cdtr_acct) in.creditor->acct_id = acct_id_string(*t.cdtr_acct);
      }
      out.push_back(std::move(in));
    }
  }
  return out;
}

Result<std::vector<StatusReport>> status_reports(const Document& doc) {
  std::vector<StatusReport> out;
  auto flatten = [&](const std::string& orig_msg_id, const PaymentTxStatus& t) {
    StatusReport r;
    r.sts_id = t.sts_id;
    r.end_to_end_id = t.orgnl_end_to_end_id;
    r.instr_id = t.orgnl_instr_id;
    r.tx_id = t.orgnl_tx_id;
    r.status = t.tx_sts;
    r.original_msg_id = orig_msg_id;
    if (!t.sts_rsn_inf.empty()) {
      const auto& s = t.sts_rsn_inf.front();
      r.reason_code = !s.rsn_cd.empty() ? s.rsn_cd : s.rsn_prtry;
      if (!s.addtl_inf.empty()) r.reason_info = s.addtl_inf.front();
    }
    out.push_back(std::move(r));
  };
  if (const auto* m = std::get_if<Pacs002>(&doc)) {
    const std::string orig = m->orgnl_grp_inf_and_sts.empty()
                                 ? std::string{}
                                 : m->orgnl_grp_inf_and_sts.front().orgnl_msg_id;
    for (const auto& t : m->txs) flatten(orig, t);
    return out;
  }
  if (const auto* m = std::get_if<Pain002>(&doc)) {
    const std::string orig =
        m->orgnl_grp_inf_and_sts ? m->orgnl_grp_inf_and_sts->orgnl_msg_id : std::string{};
    for (const auto& p : m->pmts) {
      for (const auto& t : p.txs) flatten(orig, t);
    }
    return out;
  }
  return Error::invalid_argument(
      "status_reports: document is not a pacs.002 or pain.002 status report");
}

ReconciliationSummary reconcile_instructions(
    const std::vector<CreditTransferInstruction>& instructions,
    const std::vector<StatusReport>& reports) {
  // index every identifier the builders emit for each instruction
  std::unordered_map<std::string, std::size_t> by_id;
  by_id.reserve(instructions.size() * 3);
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    const auto& p = instructions[i].payment;
    by_id[detail::end_to_end_id(p)] = i;
    by_id[detail::safe_truncate(p.id, 35)] = i;
    by_id[p.id] = i;
  }
  ReconciliationSummary sum;
  sum.results.resize(instructions.size());
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    sum.results[i].instruction = &instructions[i];
    sum.results[i].payment_id = instructions[i].payment.id;
  }
  for (const auto& r : reports) {
    auto idx = by_id.end();
    if (!r.end_to_end_id.empty()) idx = by_id.find(r.end_to_end_id);
    if (idx == by_id.end() && !r.instr_id.empty()) idx = by_id.find(r.instr_id);
    if (idx == by_id.end() && !r.tx_id.empty()) idx = by_id.find(r.tx_id);
    if (idx == by_id.end()) {
      sum.unmatched.push_back(r);
      continue;
    }
    auto& res = sum.results[idx->second];
    res.report = r;
    res.matched = true;  // last report for this instruction wins
  }
  return sum;
}

}  // namespace stellar::inline v1::iso20022
