// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "detail/messages.hpp"

#include "stellar/core/address.hpp"
#include "stellar/iso20022/validate.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <utility>

namespace stellar::inline v1::iso20022::detail {

// ---------------------------------------------------------------------------
// Shared emitters
// ---------------------------------------------------------------------------

void emit_postal_address(pugi::xml_node parent, const char* name, const PostalAddress& a) {
  auto n = parent.append_child(name);
  add_el_if(n, "StrtNm", a.strt_nm);
  add_el_if(n, "BldgNb", a.bldg_nb);
  add_el_if(n, "BldgNm", a.bldg_nm);
  add_el_if(n, "Flr", a.flr);
  add_el_if(n, "PstCd", a.pst_cd);
  add_el_if(n, "TwnNm", a.twn_nm);
  add_el_if(n, "TwnLctnNm", a.twn_lctn_nm);
  add_el_if(n, "DstrctNm", a.dstrct_nm);
  add_el_if(n, "CtrySubDvsn", a.ctry_sub_dvsn);
  add_el_if(n, "Ctry", a.ctry);
  for (const auto& l : a.adr_line) add_el(n, "AdrLine", l);
}

void emit_generic_id(pugi::xml_node parent, const char* name, const GenericId& id) {
  auto n = parent.append_child(name);
  add_el(n, "Id", id.id);
  if (!id.schme_cd.empty() || !id.schme_prtry.empty()) {
    auto s = n.append_child("SchmeNm");
    add_el_if(s, "Cd", id.schme_cd);
    add_el_if(s, "Prtry", id.schme_prtry);
  }
  add_el_if(n, "Issr", id.issr);
}

void emit_contact(pugi::xml_node parent, const char* name, const ContactDetails& c) {
  auto n = parent.append_child(name);
  add_el_if(n, "Nm", c.nm);
  add_el_if(n, "PhneNb", c.phne_nb);
  add_el_if(n, "MobNb", c.mob_nb);
  add_el_if(n, "EmailAdr", c.email_adr);
}

void emit_party(pugi::xml_node parent, const char* name, const PartyIdentification& p) {
  auto n = parent.append_child(name);
  add_el_if(n, "Nm", p.nm);
  if (p.pstl_adr) emit_postal_address(n, "PstlAdr", *p.pstl_adr);
  if (p.id) {
    auto id = n.append_child("Id");
    const PartyId& pi = *p.id;
    if (!pi.any_bic.empty() || !pi.lei.empty() || !pi.org_othr.empty()) {
      auto org = id.append_child("OrgId");
      add_el_if(org, "AnyBIC", pi.any_bic);
      add_el_if(org, "LEI", pi.lei);
      for (const auto& o : pi.org_othr) emit_generic_id(org, "Othr", o);
    }
    if (!pi.birth_dt.empty() || !pi.prvt_othr.empty()) {
      auto prvt = id.append_child("PrvtId");
      if (!pi.birth_dt.empty()) {
        auto d = prvt.append_child("DtAndPlcOfBirth");
        add_el(d, "BirthDt", pi.birth_dt);
        add_el_if(d, "CityOfBirth", pi.city_of_birth);
        add_el_if(d, "CtryOfBirth", pi.ctry_of_birth);
      }
      for (const auto& o : pi.prvt_othr) emit_generic_id(prvt, "Othr", o);
    }
  }
  add_el_if(n, "CtryOfRes", p.ctry_of_res);
  if (p.ctct_dtls) emit_contact(n, "CtctDtls", *p.ctct_dtls);
}

void emit_party50(pugi::xml_node parent, const char* name, const PartyIdentification& p) {
  auto n = parent.append_child(name);
  emit_party(n, "Pty", p);
}

void emit_agent(pugi::xml_node parent, const char* name, const Agent& a) {
  auto n = parent.append_child(name);
  auto f = n.append_child("FinInstnId");
  add_el_if(f, "BICFI", a.bicfi);
  if (a.clr_sys_mmb_id) {
    auto m = f.append_child("ClrSysMmbId");
    if (!a.clr_sys_mmb_id->clr_sys_cd.empty() || !a.clr_sys_mmb_id->clr_sys_prtry.empty()) {
      auto c = m.append_child("ClrSysId");
      add_el_if(c, "Cd", a.clr_sys_mmb_id->clr_sys_cd);
      add_el_if(c, "Prtry", a.clr_sys_mmb_id->clr_sys_prtry);
    }
    add_el(m, "MmbId", a.clr_sys_mmb_id->mmb_id);
  }
  add_el_if(f, "LEI", a.lei);
  add_el_if(f, "Nm", a.nm);
  if (a.pstl_adr) emit_postal_address(f, "PstlAdr", *a.pstl_adr);
  if (a.othr) emit_generic_id(f, "Othr", *a.othr);
}

void emit_cash_account(pugi::xml_node parent, const char* name, const CashAccount& a) {
  auto n = parent.append_child(name);
  if (a.id) {
    auto id = n.append_child("Id");
    if (!a.id->iban.empty()) {
      add_el(id, "IBAN", a.id->iban);
    } else if (a.id->othr) {
      emit_generic_id(id, "Othr", *a.id->othr);
    }
  }
  if (!a.tp_cd.empty() || !a.tp_prtry.empty()) {
    auto tp = n.append_child("Tp");
    add_el_if(tp, "Cd", a.tp_cd);
    add_el_if(tp, "Prtry", a.tp_prtry);
  }
  add_el_if(n, "Ccy", a.ccy);
  add_el_if(n, "Nm", a.nm);
  if (a.prxy) {
    auto px = n.append_child("Prxy");
    if (!a.prxy->tp_prtry.empty()) {
      auto tp = px.append_child("Tp");
      add_el(tp, "Prtry", a.prxy->tp_prtry);
    }
    add_el(px, "Id", a.prxy->id);
  }
}

void emit_pmt_id(pugi::xml_node parent, const PaymentIdentification& id) {
  auto n = parent.append_child("PmtId");
  add_el_if(n, "InstrId", id.instr_id);
  add_el(n, "EndToEndId", id.end_to_end_id);
  add_el_if(n, "TxId", id.tx_id);
  add_el_if(n, "UETR", id.uetr);
  add_el_if(n, "ClrSysRef", id.clr_sys_ref);
}

void emit_pmt_id6(pugi::xml_node parent, const PaymentIdentification6& id) {
  auto n = parent.append_child("PmtId");
  add_el_if(n, "InstrId", id.instr_id);
  add_el(n, "EndToEndId", id.end_to_end_id);
  add_el_if(n, "UETR", id.uetr);
}

void emit_purpose(pugi::xml_node parent, const Purpose& p) {
  auto n = parent.append_child("Purp");
  add_el_if(n, "Cd", p.cd);
  add_el_if(n, "Prtry", p.prtry);
}

void emit_rmt_inf(pugi::xml_node parent, const RemittanceInfo& r) {
  auto n = parent.append_child("RmtInf");
  for (const auto& l : r.ustrd) add_el(n, "Ustrd", l);
  for (const auto& s : r.strd) {
    auto sd = n.append_child("Strd");
    auto d = sd.append_child("RfrdDocInf");
    add_el(d, "Nb", s.rfrd_doc_nb);
  }
}

/// One SplmtryData{Envlp{Asset{Cd,Issr?,Amt?}}} per supplementary asset —
/// the proprietary block carrying non-ISO Stellar asset detail.
void emit_splmtry_assets(pugi::xml_node tx, const std::vector<SupplementaryAsset>& assets) {
  for (const auto& a : assets) {
    auto s = tx.append_child("SplmtryData");
    auto e = s.append_child("Envlp");
    auto as = e.append_child("Asset");
    add_el(as, "Cd", a.cd);
    add_el_if(as, "Issr", a.issr);
    add_el_if(as, "Amt", a.amt);
  }
}

void emit_amount(pugi::xml_node parent, const char* name, const CcyAmount& a) {
  add_amt(parent, name, a.ccy, a.value);
}

void emit_amount_if(pugi::xml_node parent, const char* name, const std::optional<CcyAmount>& a) {
  if (a) emit_amount(parent, name, *a);
}

void emit_orgnl_grp_inf(pugi::xml_node parent, const char* name, const OriginalGroupInfo& g) {
  auto n = parent.append_child(name);
  add_el(n, "OrgnlMsgId", g.orgnl_msg_id);
  add_el(n, "OrgnlMsgNmId", g.orgnl_msg_nm_id);
  add_el_if(n, "OrgnlCreDtTm", g.orgnl_cre_dt_tm);
}

void emit_orgnl_tx_ref(pugi::xml_node parent, const OriginalTxRef& r) {
  auto n = parent.append_child("OrgnlTxRef");
  emit_amount_if(n, "IntrBkSttlmAmt", r.intrbk_sttlm_amt);
  add_el_if(n, "IntrBkSttlmDt", r.intrbk_sttlm_dt);
  if (r.rmt_inf) emit_rmt_inf(n, *r.rmt_inf);
  if (r.ultmt_dbtr) emit_party50(n, "UltmtDbtr", *r.ultmt_dbtr);
  if (r.dbtr) emit_party50(n, "Dbtr", *r.dbtr);
  if (r.dbtr_acct) emit_cash_account(n, "DbtrAcct", *r.dbtr_acct);
  if (r.dbtr_agt) emit_agent(n, "DbtrAgt", *r.dbtr_agt);
  if (r.cdtr_agt) emit_agent(n, "CdtrAgt", *r.cdtr_agt);
  if (r.cdtr) emit_party50(n, "Cdtr", *r.cdtr);
  if (r.cdtr_acct) emit_cash_account(n, "CdtrAcct", *r.cdtr_acct);
  if (r.ultmt_cdtr) emit_party50(n, "UltmtCdtr", *r.ultmt_cdtr);
  if (r.purp) emit_purpose(n, *r.purp);
}

/// MsgId/CreDtTm prefix shared by every GroupHeader variant.
void emit_grp_hdr_base(pugi::xml_node parent, const GroupHeader& h) {
  auto n = parent.append_child("GrpHdr");
  add_el(n, "MsgId", h.msg_id);
  add_el(n, "CreDtTm", h.cre_dt_tm);
  // callers append the remaining, version-specific children in XSD order
}

// ---------------------------------------------------------------------------
// Shared readers
// ---------------------------------------------------------------------------

void read_postal_address(pugi::xml_node n, PostalAddress& a) {
  a.strt_nm = child_text(n, "StrtNm");
  a.bldg_nb = child_text(n, "BldgNb");
  a.bldg_nm = child_text(n, "BldgNm");
  a.flr = child_text(n, "Flr");
  a.pst_cd = child_text(n, "PstCd");
  a.twn_nm = child_text(n, "TwnNm");
  a.twn_lctn_nm = child_text(n, "TwnLctnNm");
  a.dstrct_nm = child_text(n, "DstrctNm");
  a.ctry_sub_dvsn = child_text(n, "CtrySubDvsn");
  a.ctry = child_text(n, "Ctry");
  for (auto l : n.children("AdrLine")) {
    if (auto v = opt_text(l)) a.adr_line.push_back(std::move(*v));
  }
}

void read_generic_id(pugi::xml_node n, GenericId& id) {
  id.id = child_text(n, "Id");
  auto s = n.child("SchmeNm");
  id.schme_cd = child_text(s, "Cd");
  id.schme_prtry = child_text(s, "Prtry");
  id.issr = child_text(n, "Issr");
}

void read_contact(pugi::xml_node n, ContactDetails& c) {
  c.nm = child_text(n, "Nm");
  c.phne_nb = child_text(n, "PhneNb");
  c.mob_nb = child_text(n, "MobNb");
  c.email_adr = child_text(n, "EmailAdr");
}

void read_party(pugi::xml_node n, PartyIdentification& p) {
  p.nm = child_text(n, "Nm");
  if (auto a = n.child("PstlAdr")) {
    p.pstl_adr.emplace();
    read_postal_address(a, *p.pstl_adr);
  }
  if (auto id = n.child("Id")) {
    p.id.emplace();
    if (auto org = id.child("OrgId")) {
      p.id->any_bic = child_text(org, "AnyBIC");
      p.id->lei = child_text(org, "LEI");
      for (auto o : org.children("Othr")) {
        p.id->org_othr.emplace_back();
        read_generic_id(o, p.id->org_othr.back());
      }
    }
    if (auto prvt = id.child("PrvtId")) {
      if (auto d = prvt.child("DtAndPlcOfBirth")) {
        p.id->birth_dt = child_text(d, "BirthDt");
        p.id->city_of_birth = child_text(d, "CityOfBirth");
        p.id->ctry_of_birth = child_text(d, "CtryOfBirth");
      }
      for (auto o : prvt.children("Othr")) {
        p.id->prvt_othr.emplace_back();
        read_generic_id(o, p.id->prvt_othr.back());
      }
    }
  }
  p.ctry_of_res = child_text(n, "CtryOfRes");
  if (auto c = n.child("CtctDtls")) {
    p.ctct_dtls.emplace();
    read_contact(c, *p.ctct_dtls);
  }
}

void read_party50(pugi::xml_node n, PartyIdentification& p) {
  if (auto pty = n.child("Pty")) {
    read_party(pty, p);
    return;
  }
  // tolerate a bare PartyIdentification272 on read
  read_party(n, p);
}

void read_agent(pugi::xml_node n, Agent& a) {
  auto f = n.child("FinInstnId");
  if (!f) f = n;  // tolerate unwrapped FinInstnId on read
  a.bicfi = child_text(f, "BICFI");
  if (auto m = f.child("ClrSysMmbId")) {
    a.clr_sys_mmb_id.emplace();
    auto c = m.child("ClrSysId");
    a.clr_sys_mmb_id->clr_sys_cd = child_text(c, "Cd");
    a.clr_sys_mmb_id->clr_sys_prtry = child_text(c, "Prtry");
    a.clr_sys_mmb_id->mmb_id = child_text(m, "MmbId");
  }
  a.lei = child_text(f, "LEI");
  a.nm = child_text(f, "Nm");
  if (auto pa = f.child("PstlAdr")) {
    a.pstl_adr.emplace();
    read_postal_address(pa, *a.pstl_adr);
  }
  if (auto o = f.child("Othr")) {
    a.othr.emplace();
    read_generic_id(o, *a.othr);
  }
}

void read_cash_account(pugi::xml_node n, CashAccount& a) {
  if (auto id = n.child("Id")) {
    a.id.emplace();
    a.id->iban = child_text(id, "IBAN");
    if (auto o = id.child("Othr")) {
      a.id->othr.emplace();
      read_generic_id(o, *a.id->othr);
    }
  }
  if (auto tp = n.child("Tp")) {
    a.tp_cd = child_text(tp, "Cd");
    a.tp_prtry = child_text(tp, "Prtry");
  }
  a.ccy = child_text(n, "Ccy");
  a.nm = child_text(n, "Nm");
  if (auto px = n.child("Prxy")) {
    a.prxy.emplace();
    if (auto tp = px.child("Tp")) a.prxy->tp_prtry = child_text(tp, "Prtry");
    a.prxy->id = child_text(px, "Id");
  }
}

void read_pmt_id(pugi::xml_node n, PaymentIdentification& id, const std::string& path,
                 std::vector<std::string>& errs) {
  id.instr_id = child_text(n, "InstrId");
  req_text(n, "EndToEndId", id.end_to_end_id, path, errs);
  id.tx_id = child_text(n, "TxId");
  id.uetr = child_text(n, "UETR");
  id.clr_sys_ref = child_text(n, "ClrSysRef");
}

void read_rmt_inf(pugi::xml_node n, RemittanceInfo& r) {
  for (auto u : n.children("Ustrd")) {
    if (auto v = opt_text(u)) r.ustrd.push_back(std::move(*v));
  }
  for (auto s : n.children("Strd")) {
    if (auto d = s.child("RfrdDocInf")) {
      StructuredRemittance sr;
      sr.rfrd_doc_nb = child_text(d, "Nb");
      r.strd.push_back(std::move(sr));
    }
  }
}

std::vector<SupplementaryAsset> read_splmtry_assets(pugi::xml_node tx) {
  std::vector<SupplementaryAsset> out;
  for (auto s : tx.children("SplmtryData")) {
    if (auto a = s.child("Envlp").child("Asset")) {
      SupplementaryAsset sa;
      sa.cd = child_text(a, "Cd");
      sa.issr = child_text(a, "Issr");
      sa.amt = child_text(a, "Amt");
      out.push_back(std::move(sa));
    }
  }
  return out;
}

CcyAmount read_amount(pugi::xml_node n) {
  CcyAmount a;
  a.ccy = n.attribute("Ccy").value();
  a.value = opt_text(n).value_or("");
  return a;
}

void read_orgnl_grp_inf(pugi::xml_node n, OriginalGroupInfo& g, const std::string& path,
                        std::vector<std::string>& errs) {
  req_text(n, "OrgnlMsgId", g.orgnl_msg_id, path, errs);
  req_text(n, "OrgnlMsgNmId", g.orgnl_msg_nm_id, path, errs);
  g.orgnl_cre_dt_tm = child_text(n, "OrgnlCreDtTm");
}

void read_orgnl_tx_ref(pugi::xml_node n, OriginalTxRef& r) {
  if (auto a = n.child("IntrBkSttlmAmt")) r.intrbk_sttlm_amt = read_amount(a);
  r.intrbk_sttlm_dt = child_text(n, "IntrBkSttlmDt");
  if (auto ri = n.child("RmtInf")) {
    r.rmt_inf.emplace();
    read_rmt_inf(ri, *r.rmt_inf);
  }
  if (auto p = n.child("UltmtDbtr")) {
    r.ultmt_dbtr.emplace();
    read_party50(p, *r.ultmt_dbtr);
  }
  if (auto p = n.child("Dbtr")) {
    r.dbtr.emplace();
    read_party50(p, *r.dbtr);
  }
  if (auto a = n.child("DbtrAcct")) {
    r.dbtr_acct.emplace();
    read_cash_account(a, *r.dbtr_acct);
  }
  if (auto a = n.child("DbtrAgt")) {
    r.dbtr_agt.emplace();
    read_agent(a, *r.dbtr_agt);
  }
  if (auto a = n.child("CdtrAgt")) {
    r.cdtr_agt.emplace();
    read_agent(a, *r.cdtr_agt);
  }
  if (auto p = n.child("Cdtr")) {
    r.cdtr.emplace();
    read_party50(p, *r.cdtr);
  }
  if (auto a = n.child("CdtrAcct")) {
    r.cdtr_acct.emplace();
    read_cash_account(a, *r.cdtr_acct);
  }
  if (auto p = n.child("UltmtCdtr")) {
    r.ultmt_cdtr.emplace();
    read_party50(p, *r.ultmt_cdtr);
  }
  if (auto p = n.child("Purp")) {
    r.purp.emplace();
    r.purp->cd = child_text(p, "Cd");
    r.purp->prtry = child_text(p, "Prtry");
  }
}

void read_grp_hdr_base(pugi::xml_node parent, GroupHeader& h, const std::string& path,
                       std::vector<std::string>& errs) {
  req_text(parent, "MsgId", h.msg_id, path, errs);
  req_text(parent, "CreDtTm", h.cre_dt_tm, path, errs);
}

// ---------------------------------------------------------------------------
// Shared camt Ntry emit/read
// ---------------------------------------------------------------------------

void emit_entry_tx(pugi::xml_node parent, const Entry& e) {
  auto n = parent.append_child("Ntry");
  add_el_if(n, "NtryRef", e.ntry_ref);
  emit_amount(n, "Amt", e.amt);
  add_el(n, "CdtDbtInd", e.cdt_dbt_ind);
  add_el_if(n, "RvslInd", e.rvsl_ind);
  {
    // Sts is EntryStatus1Choice: <Sts><Cd>BOOK</Cd></Sts>
    auto s = n.append_child("Sts");
    add_el(s, "Cd", e.sts);
  }
  if (!e.bookg_dt.empty()) {
    auto d = n.append_child("BookgDt");
    add_el(d, "Dt", e.bookg_dt);
  }
  if (!e.val_dt.empty()) {
    auto d = n.append_child("ValDt");
    add_el(d, "Dt", e.val_dt);
  }
  add_el_if(n, "AcctSvcrRef", e.acct_svcr_ref);
  auto btc = n.append_child("BkTxCd");
  auto pr = btc.append_child("Prtry");
  add_el(pr, "Cd", e.bk_tx_cd_prtry.empty() ? "PMNT" : e.bk_tx_cd_prtry);
  add_el_if(pr, "Issr", e.bk_tx_cd_issr);
  for (const auto& td : e.tx_dtls) {
    auto d = n.append_child("NtryDtls");
    auto t = d.append_child("TxDtls");
    if (td.refs) {
      auto r = t.append_child("Refs");
      add_el_if(r, "MsgId", td.refs->msg_id);
      add_el_if(r, "AcctSvcrRef", td.refs->acct_svcr_ref);
      add_el_if(r, "PmtInfId", td.refs->pmt_inf_id);
      add_el_if(r, "InstrId", td.refs->instr_id);
      add_el_if(r, "EndToEndId", td.refs->end_to_end_id);
      add_el_if(r, "UETR", td.refs->uetr);
      add_el_if(r, "TxId", td.refs->tx_id);
      add_el_if(r, "ClrSysRef", td.refs->clr_sys_ref);
      add_el_if(r, "RtrId", td.refs->rtr_id);
    }
    emit_amount_if(t, "Amt", td.amt);
    add_el_if(t, "CdtDbtInd", td.cdt_dbt_ind);
    if (td.rltd_pties) {
      auto p = t.append_child("RltdPties");
      if (td.rltd_pties->initg_pty) emit_party50(p, "InitgPty", *td.rltd_pties->initg_pty);
      if (td.rltd_pties->dbtr) emit_party50(p, "Dbtr", *td.rltd_pties->dbtr);
      if (td.rltd_pties->dbtr_acct) emit_cash_account(p, "DbtrAcct", *td.rltd_pties->dbtr_acct);
      if (td.rltd_pties->ultmt_dbtr) emit_party50(p, "UltmtDbtr", *td.rltd_pties->ultmt_dbtr);
      if (td.rltd_pties->cdtr) emit_party50(p, "Cdtr", *td.rltd_pties->cdtr);
      if (td.rltd_pties->cdtr_acct) emit_cash_account(p, "CdtrAcct", *td.rltd_pties->cdtr_acct);
      if (td.rltd_pties->ultmt_cdtr) emit_party50(p, "UltmtCdtr", *td.rltd_pties->ultmt_cdtr);
    }
    if (td.rmt_inf) emit_rmt_inf(t, *td.rmt_inf);
    add_el_if(t, "AddtlTxInf", td.addtl_tx_inf);
    emit_splmtry_assets(t, td.splmtry_assets);
  }
  add_el_if(n, "AddtlNtryInf", e.addtl_ntry_inf);
}

void read_entry_tx(pugi::xml_node parent, Entry& e) {
  e.ntry_ref = child_text(parent, "NtryRef");
  if (auto a = parent.child("Amt")) e.amt = read_amount(a);
  e.cdt_dbt_ind = child_text(parent, "CdtDbtInd");
  e.rvsl_ind = child_text(parent, "RvslInd");
  if (auto s = parent.child("Sts")) {
    e.sts = child_text(s, "Cd");
    if (e.sts.empty()) e.sts = child_text(s, "Prtry");
    if (e.sts.empty()) e.sts = opt_text(s).value_or("");  // bare <Sts>X</Sts>
  }
  if (auto d = parent.child("BookgDt")) e.bookg_dt = child_text(d, "Dt");
  if (auto d = parent.child("ValDt")) e.val_dt = child_text(d, "Dt");
  e.acct_svcr_ref = child_text(parent, "AcctSvcrRef");
  if (auto btc = parent.child("BkTxCd")) {
    if (auto pr = btc.child("Prtry")) {
      e.bk_tx_cd_prtry = child_text(pr, "Cd");
      e.bk_tx_cd_issr = child_text(pr, "Issr");
    }
  }
  for (auto d : parent.children("NtryDtls")) {
    for (auto t : d.children("TxDtls")) {
      EntryTxDetails td;
      if (auto r = t.child("Refs")) {
        td.refs.emplace();
        td.refs->msg_id = child_text(r, "MsgId");
        td.refs->acct_svcr_ref = child_text(r, "AcctSvcrRef");
        td.refs->pmt_inf_id = child_text(r, "PmtInfId");
        td.refs->instr_id = child_text(r, "InstrId");
        td.refs->end_to_end_id = child_text(r, "EndToEndId");
        td.refs->uetr = child_text(r, "UETR");
        td.refs->tx_id = child_text(r, "TxId");
        td.refs->clr_sys_ref = child_text(r, "ClrSysRef");
        td.refs->rtr_id = child_text(r, "RtrId");
      }
      if (auto a = t.child("Amt")) td.amt = read_amount(a);
      td.cdt_dbt_ind = child_text(t, "CdtDbtInd");
      if (auto p = t.child("RltdPties")) {
        td.rltd_pties.emplace();
        if (auto x = p.child("InitgPty")) {
          td.rltd_pties->initg_pty.emplace();
          read_party50(x, *td.rltd_pties->initg_pty);
        }
        if (auto x = p.child("Dbtr")) {
          td.rltd_pties->dbtr.emplace();
          read_party50(x, *td.rltd_pties->dbtr);
        }
        if (auto x = p.child("DbtrAcct")) {
          td.rltd_pties->dbtr_acct.emplace();
          read_cash_account(x, *td.rltd_pties->dbtr_acct);
        }
        if (auto x = p.child("UltmtDbtr")) {
          td.rltd_pties->ultmt_dbtr.emplace();
          read_party50(x, *td.rltd_pties->ultmt_dbtr);
        }
        if (auto x = p.child("Cdtr")) {
          td.rltd_pties->cdtr.emplace();
          read_party50(x, *td.rltd_pties->cdtr);
        }
        if (auto x = p.child("CdtrAcct")) {
          td.rltd_pties->cdtr_acct.emplace();
          read_cash_account(x, *td.rltd_pties->cdtr_acct);
        }
        if (auto x = p.child("UltmtCdtr")) {
          td.rltd_pties->ultmt_cdtr.emplace();
          read_party50(x, *td.rltd_pties->ultmt_cdtr);
        }
      }
      if (auto ri = t.child("RmtInf")) {
        td.rmt_inf.emplace();
        read_rmt_inf(ri, *td.rmt_inf);
      }
      td.addtl_tx_inf = child_text(t, "AddtlTxInf");
      td.splmtry_assets = read_splmtry_assets(t);
      e.tx_dtls.push_back(std::move(td));
    }
  }
  e.addtl_ntry_inf = child_text(parent, "AddtlNtryInf");
}

// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------

std::string safe_truncate(std::string_view s, std::size_t n) {
  return s.size() <= n ? std::string(s) : std::string(s.substr(0, n));
}

std::string normalize_amount5(std::string_view amt) {
  const auto dot = amt.find('.');
  if (dot == std::string_view::npos) return std::string(amt);
  const auto frac = amt.size() - dot - 1;
  return frac <= 5 ? std::string(amt) : std::string(amt.substr(0, dot + 6));
}

/// Sum non-negative decimal strings exactly (integer-aligned digits, then
/// reinsert the point). Payments are non-negative; signs are rejected.
Result<std::string> decimal_sum(const std::vector<std::string>& amounts) {
  std::string digits;
  std::size_t frac = 0;
  for (const auto& raw : amounts) {
    const auto dot = raw.find('.');
    std::string whole = dot == std::string::npos ? raw : raw.substr(0, dot);
    std::string part = dot == std::string::npos ? "" : raw.substr(dot + 1);
    if (whole.empty()) whole = "0";
    if (whole.find('-') != std::string::npos || part.find('-') != std::string::npos ||
        !std::all_of(whole.begin(), whole.end(), [](unsigned char c) { return std::isdigit(c); }) ||
        !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c); })) {
      return Error::invalid_argument("decimal_sum: invalid amount \"" + raw + "\"");
    }
    if (part.size() > frac) {
      digits.append(part.size() - frac, '0');
      frac = part.size();
    }
    part.append(frac - part.size(), '0');
    std::string cur = whole + part;
    // strip leading zeros, then add digit-wise into `digits`
    cur.erase(0, cur.find_first_not_of('0'));
    if (cur.empty()) cur = "0";
    if (digits.empty()) digits = "0";
    std::string sum;
    int carry = 0;
    auto i = cur.rbegin(), j = digits.rbegin();
    while (i != cur.rend() || j != digits.rend() || carry) {
      int d = carry;
      if (i != cur.rend()) d += *i++ - '0';
      if (j != digits.rend()) d += *j++ - '0';
      sum.push_back(static_cast<char>('0' + d % 10));
      carry = d / 10;
    }
    digits.assign(sum.rbegin(), sum.rend());
  }
  if (digits.empty()) digits = "0";
  if (frac > 0) {
    if (digits.size() <= frac) digits.insert(0, frac - digits.size() + 1, '0');
    digits.insert(digits.size() - frac, 1, '.');
    while (digits.back() == '0') digits.pop_back();
    if (digits.back() == '.') digits.pop_back();
  }
  return digits;
}

std::string uuid_v4(RandomSource& rng) {
  auto b = rng.bytes(16);
  b[6] = static_cast<std::uint8_t>((b[6] & 0x0F) | 0x40);
  b[8] = static_cast<std::uint8_t>((b[8] & 0x3F) | 0x80);
  char out[37];
  std::snprintf(out, sizeof(out),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11],
                b[12], b[13], b[14], b[15]);
  return std::string(out, 36);
}

std::string format_isodate(std::int64_t unix_seconds) {
  std::time_t t = unix_seconds;
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char out[11];
  std::snprintf(out, sizeof(out), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return out;
}

std::string format_isodatetime(std::int64_t unix_seconds) {
  std::time_t t = unix_seconds;
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char out[21];
  std::snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return out;
}

std::pair<std::string, std::optional<SupplementaryAsset>> settlement_currency(
    std::string_view asset_code, std::string_view asset_issuer, std::string_view exact_amount) {
  std::string code(asset_code.substr(0, asset_code.find(':')));
  std::string issuer(asset_issuer);
  if (const auto colon = asset_code.find(':'); colon != std::string_view::npos) {
    issuer = std::string(asset_code.substr(colon + 1));
  }
  std::transform(code.begin(), code.end(), code.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  if (is_iso4217(code)) return {code, std::nullopt};
  return {"XXX", SupplementaryAsset{code, issuer, std::string(exact_amount)}};
}

Agent agent_by_bic(std::string_view bic) {
  Agent a;
  a.bicfi = std::string(bic);
  return a;
}

Agent agent_by_other_id(std::string_view id) {
  Agent a;
  a.othr = GenericId{std::string(id), {}, {}, {}};
  return a;
}

Agent agent_or_fallback(std::string_view bic, std::string_view fallback_id) {
  if (!bic.empty()) return agent_by_bic(bic);
  return agent_by_other_id(fallback_id.empty() ? "NOTPROVIDED" : fallback_id);
}

PartyIdentification party_identification(const Party* p, std::string_view fallback_nm) {
  PartyIdentification out;
  out.nm = std::string(fallback_nm);
  if (!p) return out;
  if (!p->name.empty()) out.nm = p->name;
  out.pstl_adr = p->address;
  if (!p->id.empty()) {
    PartyId pid;
    GenericId g;
    g.id = p->id;
    g.schme_cd = p->id_scheme;
    pid.org_othr.push_back(std::move(g));
    out.id = std::move(pid);
  }
  if (!p->phone.empty() || !p->email.empty()) {
    ContactDetails c;
    c.nm = p->name;
    c.phne_nb = p->phone;
    c.email_adr = p->email;
    out.ctct_dtls = std::move(c);
  }
  return out;
}

std::optional<CashAccount> party_account(const Party* p) {
  if (!p) return std::nullopt;
  CashAccount a;
  if (!p->acct_iban.empty()) {
    a.id = AccountId{p->acct_iban, std::nullopt};
  } else if (!p->acct_id.empty() && p->acct_id.size() <= 34) {
    a.id = AccountId{{}, GenericId{p->acct_id, {}, {}, {}}};
  } else if (!p->acct_id.empty() || !p->acct_proxy_id.empty()) {
    a.prxy = ProxyAccount{p->acct_proxy_type,
                          p->acct_proxy_id.empty() ? p->acct_id : p->acct_proxy_id};
  } else {
    return std::nullopt;
  }
  return a;
}

std::string end_to_end_id(const Payment& p) {
  if (!p.tx_hash.empty()) return safe_truncate(p.tx_hash, 35);
  return safe_truncate(p.id, 35);
}

std::string payment_asset(const Payment& p) {
  return p.asset.is_native() ? "XLM" : p.asset.code();
}

RemittanceInfo remittance_info(const std::vector<std::string>& lines, const Payment& p) {
  RemittanceInfo r;
  if (!lines.empty()) {
    r.ustrd = lines;
    return r;
  }
  const auto& m = p.memo.xdr();
  switch (m.type()) {
    case xdr::MemoType::MEMO_TEXT: {
      std::string t(m.text().begin(), m.text().end());
      if (!t.empty()) r.ustrd.push_back(std::move(t));
      break;
    }
    case xdr::MemoType::MEMO_ID:
      r.strd.push_back(StructuredRemittance{std::to_string(m.id())});
      break;
    case xdr::MemoType::MEMO_HASH: {
      const auto& h = m.hash();
      char buf[65];
      for (std::size_t i = 0; i < h.size(); ++i)
        std::snprintf(buf + i * 2, 3, "%02x", static_cast<unsigned char>(h[i]));
      r.strd.push_back(StructuredRemittance{std::string(buf, 64)});
      break;
    }
    case xdr::MemoType::MEMO_RETURN: {
      const auto& h = m.retHash();
      char buf[65];
      for (std::size_t i = 0; i < h.size(); ++i)
        std::snprintf(buf + i * 2, 3, "%02x", static_cast<unsigned char>(h[i]));
      r.strd.push_back(StructuredRemittance{std::string(buf, 64)});
      break;
    }
    default:
      break;
  }
  return r;
}

Error join_errors(std::vector<std::string> errs) {
  std::string msg;
  for (std::size_t i = 0; i < errs.size(); ++i) {
    if (i) msg += "; ";
    msg += errs[i];
  }
  return Error{ErrorCode::kProtocolError, std::move(msg)};
}

}  // namespace stellar::inline v1::iso20022::detail
