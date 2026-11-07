// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

/// @file pacs008.cpp
/// pacs.008.001.14 — FIToFICstmrCdtTrf emit/read.

#include "detail/messages.hpp"

namespace stellar::inline v1::iso20022::detail {

namespace {

void emit_grp_hdr(pugi::xml_node parent, const GroupHeader& h) {
  auto n = parent.append_child("GrpHdr");
  add_el(n, "MsgId", h.msg_id);
  add_el(n, "CreDtTm", h.cre_dt_tm);
  if (h.btch_bookg) add_el(n, "BtchBookg", *h.btch_bookg ? "true" : "false");
  add_el(n, "NbOfTxs", h.nb_of_txs);
  add_el_if(n, "CtrlSum", h.ctrl_sum);
  emit_amount_if(n, "TtlIntrBkSttlmAmt", h.ttl_intrbk_sttlm_amt);
  add_el_if(n, "IntrBkSttlmDt", h.intrbk_sttlm_dt);
  {
    auto s = n.append_child("SttlmInf");
    add_el(s, "SttlmMtd", h.sttlm_inf ? h.sttlm_inf->sttlm_mtd : "CLRG");
  }
  if (h.instg_agt) emit_agent(n, "InstgAgt", *h.instg_agt);
  if (h.instd_agt) emit_agent(n, "InstdAgt", *h.instd_agt);
}

void emit_pmt_tp_inf(pugi::xml_node parent, const PaymentTypeInfo& t) {
  auto n = parent.append_child("PmtTpInf");
  add_el_if(n, "InstrPrty", t.instr_prty);
  add_el_if(n, "ClrChanl", t.clr_chanl);
  if (!t.svc_lvl_cd.empty() || !t.svc_lvl_prtry.empty()) {
    auto s = n.append_child("SvcLvl");
    add_el_if(s, "Cd", t.svc_lvl_cd);
    add_el_if(s, "Prtry", t.svc_lvl_prtry);
  }
  if (!t.lcl_instrm_cd.empty() || !t.lcl_instrm_prtry.empty()) {
    auto s = n.append_child("LclInstrm");
    add_el_if(s, "Cd", t.lcl_instrm_cd);
    add_el_if(s, "Prtry", t.lcl_instrm_prtry);
  }
  if (!t.ctgy_purp_cd.empty() || !t.ctgy_purp_prtry.empty()) {
    auto s = n.append_child("CtgyPurp");
    add_el_if(s, "Cd", t.ctgy_purp_cd);
    add_el_if(s, "Prtry", t.ctgy_purp_prtry);
  }
}

void emit_tx(pugi::xml_node parent, const CreditTransferTx& t) {
  auto n = parent.append_child("CdtTrfTxInf");
  emit_pmt_id(n, t.pmt_id);
  if (t.pmt_tp_inf) emit_pmt_tp_inf(n, *t.pmt_tp_inf);
  emit_amount(n, "IntrBkSttlmAmt", t.intrbk_sttlm_amt);
  add_el_if(n, "IntrBkSttlmDt", t.intrbk_sttlm_dt);
  emit_amount_if(n, "InstdAmt", t.instd_amt);
  add_el(n, "ChrgBr", t.chrg_br.empty() ? "SHAR" : t.chrg_br);
  for (const auto& c : t.chrgs_inf) {
    auto ci = n.append_child("ChrgsInf");
    emit_amount(ci, "Amt", c.amt);
    emit_agent(ci, "Agt", c.agt);
    if (!c.tp_cd.empty() || !c.tp_prtry.empty()) {
      auto tp = ci.append_child("Tp");
      add_el_if(tp, "Cd", c.tp_cd);
      add_el_if(tp, "Prtry", c.tp_prtry);
    }
  }
  if (t.instg_agt) emit_agent(n, "InstgAgt", *t.instg_agt);
  if (t.instd_agt) emit_agent(n, "InstdAgt", *t.instd_agt);
  if (t.ultmt_dbtr) emit_party(n, "UltmtDbtr", *t.ultmt_dbtr);
  if (t.initg_pty) emit_party(n, "InitgPty", *t.initg_pty);
  emit_party(n, "Dbtr", t.dbtr);
  if (t.dbtr_acct) emit_cash_account(n, "DbtrAcct", *t.dbtr_acct);
  emit_agent(n, "DbtrAgt", t.dbtr_agt);
  if (t.dbtr_agt_acct) emit_cash_account(n, "DbtrAgtAcct", *t.dbtr_agt_acct);
  emit_agent(n, "CdtrAgt", t.cdtr_agt);
  if (t.cdtr_agt_acct) emit_cash_account(n, "CdtrAgtAcct", *t.cdtr_agt_acct);
  emit_party(n, "Cdtr", t.cdtr);
  if (t.cdtr_acct) emit_cash_account(n, "CdtrAcct", *t.cdtr_acct);
  if (t.ultmt_cdtr) emit_party(n, "UltmtCdtr", *t.ultmt_cdtr);
  if (t.purp) emit_purpose(n, *t.purp);
  if (t.rmt_inf) emit_rmt_inf(n, *t.rmt_inf);
  emit_splmtry_assets(n, t.splmtry_assets);
}

void read_pmt_tp_inf(pugi::xml_node n, PaymentTypeInfo& t) {
  t.instr_prty = child_text(n, "InstrPrty");
  t.clr_chanl = child_text(n, "ClrChanl");
  if (auto s = n.child("SvcLvl")) {
    t.svc_lvl_cd = child_text(s, "Cd");
    t.svc_lvl_prtry = child_text(s, "Prtry");
  }
  if (auto s = n.child("LclInstrm")) {
    t.lcl_instrm_cd = child_text(s, "Cd");
    t.lcl_instrm_prtry = child_text(s, "Prtry");
  }
  if (auto s = n.child("CtgyPurp")) {
    t.ctgy_purp_cd = child_text(s, "Cd");
    t.ctgy_purp_prtry = child_text(s, "Prtry");
  }
}

}  // namespace

void emit_pacs008(pugi::xml_node doc, const Pacs008& m) {
  auto root = doc.append_child("FIToFICstmrCdtTrf");
  emit_grp_hdr(root, m.grp_hdr);
  for (const auto& t : m.txs) emit_tx(root, t);
}

Result<Pacs008> read_pacs008(pugi::xml_node doc) {
  auto root = doc.child("FIToFICstmrCdtTrf");
  if (!root) {
    return Error{ErrorCode::kProtocolError,
            std::string("pacs.008: missing FIToFICstmrCdtTrf element")};
  }
  std::vector<std::string> errs;
  Pacs008 m;
  if (auto h = root.child("GrpHdr")) {
    read_grp_hdr_base(h, m.grp_hdr, "GrpHdr", errs);
    if (auto b = h.child("BtchBookg")) m.grp_hdr.btch_bookg = opt_text(b).value_or("") == "true";
    req_text(h, "NbOfTxs", m.grp_hdr.nb_of_txs, "GrpHdr", errs);
    m.grp_hdr.ctrl_sum = child_text(h, "CtrlSum");
    if (auto a = h.child("TtlIntrBkSttlmAmt")) m.grp_hdr.ttl_intrbk_sttlm_amt = read_amount(a);
    m.grp_hdr.intrbk_sttlm_dt = child_text(h, "IntrBkSttlmDt");
    if (auto s = h.child("SttlmInf")) {
      m.grp_hdr.sttlm_inf = SettlementInstruction{child_text(s, "SttlmMtd")};
    }
    if (auto a = h.child("InstgAgt")) {
      m.grp_hdr.instg_agt.emplace();
      read_agent(a, *m.grp_hdr.instg_agt);
    }
    if (auto a = h.child("InstdAgt")) {
      m.grp_hdr.instd_agt.emplace();
      read_agent(a, *m.grp_hdr.instd_agt);
    }
  } else {
    errs.push_back("GrpHdr: required element missing");
  }

  int i = 0;
  for (auto tx : root.children("CdtTrfTxInf")) {
    const std::string path = "CdtTrfTxInf[" + std::to_string(i++) + "]";
    CreditTransferTx t;
    if (auto p = tx.child("PmtId")) {
      read_pmt_id(p, t.pmt_id, path + "/PmtId", errs);
    } else {
      errs.push_back(path + "/PmtId: required element missing");
    }
    if (auto p = tx.child("PmtTpInf")) {
      t.pmt_tp_inf.emplace();
      read_pmt_tp_inf(p, *t.pmt_tp_inf);
    }
    if (auto a = tx.child("IntrBkSttlmAmt")) {
      t.intrbk_sttlm_amt = read_amount(a);
    } else {
      errs.push_back(path + "/IntrBkSttlmAmt: required element missing");
    }
    t.intrbk_sttlm_dt = child_text(tx, "IntrBkSttlmDt");
    if (auto a = tx.child("InstdAmt")) t.instd_amt = read_amount(a);
    req_text(tx, "ChrgBr", t.chrg_br, path, errs);
    for (auto ci : tx.children("ChrgsInf")) {
      ChargeInfo c;
      if (auto a = ci.child("Amt")) c.amt = read_amount(a);
      if (auto a = ci.child("Agt")) read_agent(a, c.agt);
      if (auto tp = ci.child("Tp")) {
        c.tp_cd = child_text(tp, "Cd");
        c.tp_prtry = child_text(tp, "Prtry");
      }
      t.chrgs_inf.push_back(std::move(c));
    }
    if (auto a = tx.child("InstgAgt")) {
      t.instg_agt.emplace();
      read_agent(a, *t.instg_agt);
    }
    if (auto a = tx.child("InstdAgt")) {
      t.instd_agt.emplace();
      read_agent(a, *t.instd_agt);
    }
    if (auto p = tx.child("UltmtDbtr")) {
      t.ultmt_dbtr.emplace();
      read_party(p, *t.ultmt_dbtr);
    }
    if (auto p = tx.child("InitgPty")) {
      t.initg_pty.emplace();
      read_party(p, *t.initg_pty);
    }
    if (auto p = tx.child("Dbtr")) {
      read_party(p, t.dbtr);
    } else {
      errs.push_back(path + "/Dbtr: required element missing");
    }
    if (auto a = tx.child("DbtrAcct")) {
      t.dbtr_acct.emplace();
      read_cash_account(a, *t.dbtr_acct);
    }
    if (auto a = tx.child("DbtrAgt")) {
      read_agent(a, t.dbtr_agt);
    } else {
      errs.push_back(path + "/DbtrAgt: required element missing");
    }
    if (auto a = tx.child("DbtrAgtAcct")) {
      t.dbtr_agt_acct.emplace();
      read_cash_account(a, *t.dbtr_agt_acct);
    }
    if (auto a = tx.child("CdtrAgt")) {
      read_agent(a, t.cdtr_agt);
    } else {
      errs.push_back(path + "/CdtrAgt: required element missing");
    }
    if (auto a = tx.child("CdtrAgtAcct")) {
      t.cdtr_agt_acct.emplace();
      read_cash_account(a, *t.cdtr_agt_acct);
    }
    if (auto p = tx.child("Cdtr")) {
      read_party(p, t.cdtr);
    } else {
      errs.push_back(path + "/Cdtr: required element missing");
    }
    if (auto a = tx.child("CdtrAcct")) {
      t.cdtr_acct.emplace();
      read_cash_account(a, *t.cdtr_acct);
    }
    if (auto p = tx.child("UltmtCdtr")) {
      t.ultmt_cdtr.emplace();
      read_party(p, *t.ultmt_cdtr);
    }
    if (auto p = tx.child("Purp")) {
      t.purp.emplace();
      t.purp->cd = child_text(p, "Cd");
      t.purp->prtry = child_text(p, "Prtry");
    }
    if (auto r = tx.child("RmtInf")) {
      t.rmt_inf.emplace();
      read_rmt_inf(r, *t.rmt_inf);
    }
    t.splmtry_assets = read_splmtry_assets(tx);
    m.txs.push_back(std::move(t));
  }
  if (!errs.empty()) return join_errors(std::move(errs));
  return m;
}

}  // namespace stellar::inline v1::iso20022::detail
