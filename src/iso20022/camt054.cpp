// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

/// @file camt054.cpp
/// camt.054.001.14 — BkToCstmrDbtCdtNtfctn emit/read.

#include "detail/messages.hpp"

namespace stellar::inline v1::iso20022::detail {

namespace {

void emit_ntfctn(pugi::xml_node parent, const Notification& s) {
  auto n = parent.append_child("Ntfctn");
  add_el(n, "Id", s.id);
  add_el_if(n, "ElctrncSeqNb", s.elctrnc_seq_nb);
  add_el_if(n, "CreDtTm", s.cre_dt_tm);
  emit_cash_account(n, "Acct", s.acct);
  for (const auto& e : s.ntrys) emit_entry_tx(n, e);
  add_el_if(n, "AddtlNtfctnInf", s.addtl_ntfctn_inf);
}

}  // namespace

void emit_camt054(pugi::xml_node doc, const Camt054& m) {
  auto root = doc.append_child("BkToCstmrDbtCdtNtfctn");
  auto h = root.append_child("GrpHdr");
  add_el(h, "MsgId", m.grp_hdr.msg_id);
  add_el(h, "CreDtTm", m.grp_hdr.cre_dt_tm);
  for (const auto& s : m.ntfctns) emit_ntfctn(root, s);
}

Result<Camt054> read_camt054(pugi::xml_node doc) {
  auto root = doc.child("BkToCstmrDbtCdtNtfctn");
  if (!root) {
    return Error{ErrorCode::kProtocolError,
            std::string("camt.054: missing BkToCstmrDbtCdtNtfctn element")};
  }
  std::vector<std::string> errs;
  Camt054 m;
  if (auto h = root.child("GrpHdr")) {
    read_grp_hdr_base(h, m.grp_hdr, "GrpHdr", errs);
  } else {
    errs.push_back("GrpHdr: required element missing");
  }
  int i = 0;
  for (auto nt : root.children("Ntfctn")) {
    const std::string path = "Ntfctn[" + std::to_string(i++) + "]";
    Notification s;
    req_text(nt, "Id", s.id, path, errs);
    s.elctrnc_seq_nb = child_text(nt, "ElctrncSeqNb");
    s.cre_dt_tm = child_text(nt, "CreDtTm");
    if (auto a = nt.child("Acct")) {
      read_cash_account(a, s.acct);
    } else {
      errs.push_back(path + "/Acct: required element missing");
    }
    for (auto e : nt.children("Ntry")) {
      Entry en;
      read_entry_tx(e, en);
      s.ntrys.push_back(std::move(en));
    }
    s.addtl_ntfctn_inf = child_text(nt, "AddtlNtfctnInf");
    m.ntfctns.push_back(std::move(s));
  }
  if (!errs.empty()) return join_errors(std::move(errs));
  return m;
}

}  // namespace stellar::inline v1::iso20022::detail
