// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

/// @file camt053.cpp
/// camt.053.001.14 — BkToCstmrStmt emit/read.

#include "detail/messages.hpp"

namespace stellar::inline v1::iso20022::detail {

namespace {

void emit_grp_hdr(pugi::xml_node parent, const GroupHeader& h) {
  auto n = parent.append_child("GrpHdr");
  add_el(n, "MsgId", h.msg_id);
  add_el(n, "CreDtTm", h.cre_dt_tm);
}

void emit_balance(pugi::xml_node parent, const Balance& b) {
  auto n = parent.append_child("Bal");
  {
    auto tp = n.append_child("Tp");
    auto cp = tp.append_child("CdOrPrtry");
    add_el_if(cp, "Cd", b.tp_cd);
    add_el_if(cp, "Prtry", b.tp_prtry);
  }
  emit_amount(n, "Amt", b.amt);
  add_el(n, "CdtDbtInd", b.cdt_dbt_ind);
  auto dt = n.append_child("Dt");
  add_el(dt, "Dt", b.dt);
}

void emit_stmt(pugi::xml_node parent, const Statement& s) {
  auto n = parent.append_child("Stmt");
  add_el(n, "Id", s.id);
  add_el_if(n, "ElctrncSeqNb", s.elctrnc_seq_nb);
  add_el_if(n, "CreDtTm", s.cre_dt_tm);
  emit_cash_account(n, "Acct", s.acct);
  for (const auto& b : s.bals) emit_balance(n, b);
  for (const auto& e : s.ntrys) emit_entry_tx(n, e);
  add_el_if(n, "AddtlStmtInf", s.addtl_stmt_inf);
}

void read_balance(pugi::xml_node n, Balance& b) {
  if (auto tp = n.child("Tp")) {
    if (auto cp = tp.child("CdOrPrtry")) {
      b.tp_cd = child_text(cp, "Cd");
      b.tp_prtry = child_text(cp, "Prtry");
    }
  }
  if (auto a = n.child("Amt")) b.amt = read_amount(a);
  b.cdt_dbt_ind = child_text(n, "CdtDbtInd");
  if (auto d = n.child("Dt")) {
    b.dt = child_text(d, "Dt");
    if (b.dt.empty()) b.dt = child_text(d, "DtTm");
  }
}

}  // namespace

void emit_camt053(pugi::xml_node doc, const Camt053& m) {
  auto root = doc.append_child("BkToCstmrStmt");
  emit_grp_hdr(root, m.grp_hdr);
  for (const auto& s : m.stmts) emit_stmt(root, s);
}

Result<Camt053> read_camt053(pugi::xml_node doc) {
  auto root = doc.child("BkToCstmrStmt");
  if (!root) {
    return Error{ErrorCode::kProtocolError, std::string("camt.053: missing BkToCstmrStmt element")};
  }
  std::vector<std::string> errs;
  Camt053 m;
  if (auto h = root.child("GrpHdr")) {
    read_grp_hdr_base(h, m.grp_hdr, "GrpHdr", errs);
  } else {
    errs.push_back("GrpHdr: required element missing");
  }
  int i = 0;
  for (auto st : root.children("Stmt")) {
    const std::string path = "Stmt[" + std::to_string(i++) + "]";
    Statement s;
    req_text(st, "Id", s.id, path, errs);
    s.elctrnc_seq_nb = child_text(st, "ElctrncSeqNb");
    s.cre_dt_tm = child_text(st, "CreDtTm");
    if (auto a = st.child("Acct")) {
      read_cash_account(a, s.acct);
    } else {
      errs.push_back(path + "/Acct: required element missing");
    }
    for (auto b : st.children("Bal")) {
      Balance bal;
      read_balance(b, bal);
      s.bals.push_back(std::move(bal));
    }
    for (auto e : st.children("Ntry")) {
      Entry en;
      read_entry_tx(e, en);
      s.ntrys.push_back(std::move(en));
    }
    s.addtl_stmt_inf = child_text(st, "AddtlStmtInf");
    m.stmts.push_back(std::move(s));
  }
  if (!errs.empty()) return join_errors(std::move(errs));
  return m;
}

}  // namespace stellar::inline v1::iso20022::detail
