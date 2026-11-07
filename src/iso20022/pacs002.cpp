// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

/// @file pacs002.cpp
/// pacs.002.001.16 — FIToFIPmtStsRpt emit/read.

#include "detail/messages.hpp"

namespace stellar::inline v1::iso20022::detail {

namespace {

void emit_grp_hdr(pugi::xml_node parent, const GroupHeader& h) {
  auto n = parent.append_child("GrpHdr");
  add_el(n, "MsgId", h.msg_id);
  add_el(n, "CreDtTm", h.cre_dt_tm);
  if (h.instg_agt) emit_agent(n, "InstgAgt", *h.instg_agt);
  if (h.instd_agt) emit_agent(n, "InstdAgt", *h.instd_agt);
}

void emit_sts_rsn_inf(pugi::xml_node parent, const StatusReasonInfo& r) {
  auto n = parent.append_child("StsRsnInf");
  if (r.orgtr) emit_party(n, "Orgtr", *r.orgtr);
  if (!r.rsn_cd.empty() || !r.rsn_prtry.empty()) {
    auto s = n.append_child("Rsn");
    add_el_if(s, "Cd", r.rsn_cd);
    add_el_if(s, "Prtry", r.rsn_prtry);
  }
  for (const auto& l : r.addtl_inf) add_el(n, "AddtlInf", l);
}

void emit_tx_sts(pugi::xml_node parent, const PaymentTxStatus& t, bool with_orgnl_tx_id = true) {
  auto n = parent.append_child("TxInfAndSts");
  add_el_if(n, "StsId", t.sts_id);
  if (t.orgnl_grp_inf) emit_orgnl_grp_inf(n, "OrgnlGrpInf", *t.orgnl_grp_inf);
  add_el_if(n, "OrgnlInstrId", t.orgnl_instr_id);
  add_el_if(n, "OrgnlEndToEndId", t.orgnl_end_to_end_id);
  if (with_orgnl_tx_id) add_el_if(n, "OrgnlTxId", t.orgnl_tx_id);
  add_el_if(n, "OrgnlUETR", t.orgnl_uetr);
  add_el_if(n, "TxSts", t.tx_sts);
  for (const auto& r : t.sts_rsn_inf) emit_sts_rsn_inf(n, r);
  add_el_if(n, "AcctSvcrRef", t.acct_svcr_ref);
  add_el_if(n, "ClrSysRef", t.clr_sys_ref);
  if (t.orgnl_tx_ref) emit_orgnl_tx_ref(n, *t.orgnl_tx_ref);
}

void read_sts_rsn_inf(pugi::xml_node n, StatusReasonInfo& r) {
  if (auto p = n.child("Orgtr")) {
    r.orgtr.emplace();
    read_party(p, *r.orgtr);
  }
  if (auto s = n.child("Rsn")) {
    r.rsn_cd = child_text(s, "Cd");
    r.rsn_prtry = child_text(s, "Prtry");
  }
  for (auto l : n.children("AddtlInf")) {
    if (auto v = opt_text(l)) r.addtl_inf.push_back(std::move(*v));
  }
}

void read_tx_sts(pugi::xml_node n, PaymentTxStatus& t) {
  t.sts_id = child_text(n, "StsId");
  if (auto g = n.child("OrgnlGrpInf")) {
    t.orgnl_grp_inf.emplace();
    std::vector<std::string> ignored;  // inner required fields tolerated on read
    read_orgnl_grp_inf(g, *t.orgnl_grp_inf, "TxInfAndSts/OrgnlGrpInf", ignored);
  }
  t.orgnl_instr_id = child_text(n, "OrgnlInstrId");
  t.orgnl_end_to_end_id = child_text(n, "OrgnlEndToEndId");
  t.orgnl_tx_id = child_text(n, "OrgnlTxId");
  t.orgnl_uetr = child_text(n, "OrgnlUETR");
  t.tx_sts = child_text(n, "TxSts");
  for (auto s : n.children("StsRsnInf")) {
    StatusReasonInfo r;
    read_sts_rsn_inf(s, r);
    t.sts_rsn_inf.push_back(std::move(r));
  }
  t.acct_svcr_ref = child_text(n, "AcctSvcrRef");
  t.clr_sys_ref = child_text(n, "ClrSysRef");
  if (auto r = n.child("OrgnlTxRef")) {
    t.orgnl_tx_ref.emplace();
    read_orgnl_tx_ref(r, *t.orgnl_tx_ref);
  }
}

}  // namespace

void emit_pacs002(pugi::xml_node doc, const Pacs002& m) {
  auto root = doc.append_child("FIToFIPmtStsRpt");
  emit_grp_hdr(root, m.grp_hdr);
  for (const auto& g : m.orgnl_grp_inf_and_sts) emit_orgnl_grp_inf(root, "OrgnlGrpInfAndSts", g);
  for (const auto& t : m.txs) emit_tx_sts(root, t);
}

Result<Pacs002> read_pacs002(pugi::xml_node doc) {
  auto root = doc.child("FIToFIPmtStsRpt");
  if (!root) {
    return Error{ErrorCode::kProtocolError,
                 std::string("pacs.002: missing FIToFIPmtStsRpt element")};
  }
  std::vector<std::string> errs;
  Pacs002 m;
  if (auto h = root.child("GrpHdr")) {
    read_grp_hdr_base(h, m.grp_hdr, "GrpHdr", errs);
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
  for (auto g : root.children("OrgnlGrpInfAndSts")) {
    OriginalGroupInfo og;
    read_orgnl_grp_inf(g, og, "OrgnlGrpInfAndSts", errs);
    m.orgnl_grp_inf_and_sts.push_back(std::move(og));
  }
  for (auto t : root.children("TxInfAndSts")) {
    PaymentTxStatus s;
    read_tx_sts(t, s);
    m.txs.push_back(std::move(s));
  }
  if (!errs.empty()) return join_errors(std::move(errs));
  return m;
}

// Shared by pain.002 (same TxInfAndSts shape minus OrgnlTxId).
void emit_pacs002_tx_sts(pugi::xml_node parent, const PaymentTxStatus& t) {
  emit_tx_sts(parent, t);
}
void emit_pain002_tx_sts(pugi::xml_node parent, const PaymentTxStatus& t) {
  emit_tx_sts(parent, t, false);
}
void read_pacs002_tx_sts(pugi::xml_node n, PaymentTxStatus& t) {
  read_tx_sts(n, t);
}

}  // namespace stellar::inline v1::iso20022::detail
