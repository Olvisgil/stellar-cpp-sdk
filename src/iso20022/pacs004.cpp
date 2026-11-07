// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

/// @file pacs004.cpp
/// pacs.004.001.15 — PmtRtr emit/read.

#include "detail/messages.hpp"

namespace stellar::inline v1::iso20022::detail {

namespace {

// GroupHeader123: MsgId, CreDtTm, Authstn?, BtchBookg?, NbOfTxs, CtrlSum?,
// GrpRtr?, TtlRtrdIntrBkSttlmAmt?, IntrBkSttlmDt?, SttlmInf, PmtTpInf?,
// InstgAgt?, InstdAgt?
void emit_grp_hdr(pugi::xml_node parent, const GroupHeader& h) {
  auto n = parent.append_child("GrpHdr");
  add_el(n, "MsgId", h.msg_id);
  add_el(n, "CreDtTm", h.cre_dt_tm);
  if (h.btch_bookg) add_el(n, "BtchBookg", *h.btch_bookg ? "true" : "false");
  add_el(n, "NbOfTxs", h.nb_of_txs.empty() ? "0" : h.nb_of_txs);
  add_el_if(n, "CtrlSum", h.ctrl_sum);
  emit_amount_if(n, "TtlRtrdIntrBkSttlmAmt", h.ttl_intrbk_sttlm_amt);
  add_el_if(n, "IntrBkSttlmDt", h.intrbk_sttlm_dt);
  {
    auto s = n.append_child("SttlmInf");
    add_el(s, "SttlmMtd", h.sttlm_inf ? h.sttlm_inf->sttlm_mtd : "CLRG");
  }
  if (h.instg_agt) emit_agent(n, "InstgAgt", *h.instg_agt);
  if (h.instd_agt) emit_agent(n, "InstdAgt", *h.instd_agt);
}

void emit_rtr_rsn_inf(pugi::xml_node parent, const ReturnReasonInfo& r) {
  auto n = parent.append_child("RtrRsnInf");
  if (r.orgtr) emit_party(n, "Orgtr", *r.orgtr);
  if (!r.rsn_cd.empty() || !r.rsn_prtry.empty()) {
    auto s = n.append_child("Rsn");
    add_el_if(s, "Cd", r.rsn_cd);
    add_el_if(s, "Prtry", r.rsn_prtry);
  }
  for (const auto& l : r.addtl_inf) add_el(n, "AddtlInf", l);
}

void emit_tx(pugi::xml_node parent, const PaymentReturnTx& t) {
  auto n = parent.append_child("TxInf");
  add_el_if(n, "RtrId", t.rtr_id);
  if (t.orgnl_grp_inf) emit_orgnl_grp_inf(n, "OrgnlGrpInf", *t.orgnl_grp_inf);
  add_el_if(n, "OrgnlInstrId", t.orgnl_instr_id);
  add_el_if(n, "OrgnlEndToEndId", t.orgnl_end_to_end_id);
  add_el_if(n, "OrgnlTxId", t.orgnl_tx_id);
  add_el_if(n, "OrgnlUETR", t.orgnl_uetr);
  emit_amount(n, "RtrdIntrBkSttlmAmt", t.rtrd_intrbk_sttlm_amt);
  add_el_if(n, "IntrBkSttlmDt", t.intrbk_sttlm_dt);
  add_el_if(n, "ChrgBr", t.chrg_br);
  for (const auto& r : t.rtr_rsn_inf) emit_rtr_rsn_inf(n, r);
  if (t.orgnl_tx_ref) emit_orgnl_tx_ref(n, *t.orgnl_tx_ref);
}

}  // namespace

void emit_pacs004(pugi::xml_node doc, const Pacs004& m) {
  auto root = doc.append_child("PmtRtr");
  emit_grp_hdr(root, m.grp_hdr);
  if (m.orgnl_grp_inf) emit_orgnl_grp_inf(root, "OrgnlGrpInf", *m.orgnl_grp_inf);
  for (const auto& t : m.txs) emit_tx(root, t);
}

Result<Pacs004> read_pacs004(pugi::xml_node doc) {
  auto root = doc.child("PmtRtr");
  if (!root) {
    return Error{ErrorCode::kProtocolError, std::string("pacs.004: missing PmtRtr element")};
  }
  std::vector<std::string> errs;
  Pacs004 m;
  if (auto h = root.child("GrpHdr")) {
    read_grp_hdr_base(h, m.grp_hdr, "GrpHdr", errs);
    if (auto b = h.child("BtchBookg")) m.grp_hdr.btch_bookg = opt_text(b).value_or("") == "true";
    m.grp_hdr.nb_of_txs = child_text(h, "NbOfTxs");
    m.grp_hdr.ctrl_sum = child_text(h, "CtrlSum");
    if (auto a = h.child("TtlRtrdIntrBkSttlmAmt"))
      m.grp_hdr.ttl_intrbk_sttlm_amt = read_amount(a);
    m.grp_hdr.intrbk_sttlm_dt = child_text(h, "IntrBkSttlmDt");
    if (auto s = h.child("SttlmInf"))
      m.grp_hdr.sttlm_inf = SettlementInstruction{child_text(s, "SttlmMtd")};
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
  if (auto g = root.child("OrgnlGrpInf")) {
    m.orgnl_grp_inf.emplace();
    read_orgnl_grp_inf(g, *m.orgnl_grp_inf, "OrgnlGrpInf", errs);
  }
  int i = 0;
  for (auto tx : root.children("TxInf")) {
    const std::string path = "TxInf[" + std::to_string(i++) + "]";
    PaymentReturnTx t;
    t.rtr_id = child_text(tx, "RtrId");
    if (auto g = tx.child("OrgnlGrpInf")) {
      t.orgnl_grp_inf.emplace();
      read_orgnl_grp_inf(g, *t.orgnl_grp_inf, path + "/OrgnlGrpInf", errs);
    }
    t.orgnl_instr_id = child_text(tx, "OrgnlInstrId");
    t.orgnl_end_to_end_id = child_text(tx, "OrgnlEndToEndId");
    t.orgnl_tx_id = child_text(tx, "OrgnlTxId");
    t.orgnl_uetr = child_text(tx, "OrgnlUETR");
    if (auto a = tx.child("RtrdIntrBkSttlmAmt")) {
      t.rtrd_intrbk_sttlm_amt = read_amount(a);
    } else {
      errs.push_back(path + "/RtrdIntrBkSttlmAmt: required element missing");
    }
    t.intrbk_sttlm_dt = child_text(tx, "IntrBkSttlmDt");
    t.chrg_br = child_text(tx, "ChrgBr");
    for (auto r : tx.children("RtrRsnInf")) {
      ReturnReasonInfo ri;
      if (auto p = r.child("Orgtr")) {
        ri.orgtr.emplace();
        read_party(p, *ri.orgtr);
      }
      if (auto s = r.child("Rsn")) {
        ri.rsn_cd = child_text(s, "Cd");
        ri.rsn_prtry = child_text(s, "Prtry");
      }
      for (auto l : r.children("AddtlInf")) {
        if (auto v = opt_text(l)) ri.addtl_inf.push_back(std::move(*v));
      }
      t.rtr_rsn_inf.push_back(std::move(ri));
    }
    if (auto r = tx.child("OrgnlTxRef")) {
      t.orgnl_tx_ref.emplace();
      read_orgnl_tx_ref(r, *t.orgnl_tx_ref);
    }
    m.txs.push_back(std::move(t));
  }
  if (!errs.empty()) return join_errors(std::move(errs));
  return m;
}

}  // namespace stellar::inline v1::iso20022::detail
