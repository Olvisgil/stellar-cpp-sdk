// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

/// @file pain002.cpp
/// pain.002.001.15 — CstmrPmtStsRpt emit/read. Shares the TxInfAndSts shape
/// with pacs.002; the pain.002 envelope adds OrgnlPmtInfAndSts grouping.

#include "detail/messages.hpp"

namespace stellar::inline v1::iso20022::detail {

namespace {

// pain.002 GroupHeader: MsgId, CreDtTm, InitgPty?, FwdgAgt?, DbtAgt?, CdtAgt?
void emit_grp_hdr(pugi::xml_node parent, const GroupHeader& h) {
  auto n = parent.append_child("GrpHdr");
  add_el(n, "MsgId", h.msg_id);
  add_el(n, "CreDtTm", h.cre_dt_tm);
  if (h.initg_pty) emit_party(n, "InitgPty", *h.initg_pty);
  if (h.instg_agt) emit_agent(n, "FwdgAgt", *h.instg_agt);
}

void emit_pmt_sts(pugi::xml_node parent, const OriginalPaymentInstruction& p) {
  auto n = parent.append_child("OrgnlPmtInfAndSts");
  add_el(n, "OrgnlPmtInfId", p.orgnl_pmt_inf_id);
  for (const auto& t : p.txs) emit_pain002_tx_sts(n, t);
}

}  // namespace

void emit_pain002(pugi::xml_node doc, const Pain002& m) {
  auto root = doc.append_child("CstmrPmtStsRpt");
  emit_grp_hdr(root, m.grp_hdr);
  if (m.orgnl_grp_inf_and_sts)
    emit_orgnl_grp_inf(root, "OrgnlGrpInfAndSts", *m.orgnl_grp_inf_and_sts);
  for (const auto& p : m.pmts) emit_pmt_sts(root, p);
}

Result<Pain002> read_pain002(pugi::xml_node doc) {
  auto root = doc.child("CstmrPmtStsRpt");
  if (!root) {
    return Error{ErrorCode::kProtocolError,
                 std::string("pain.002: missing CstmrPmtStsRpt element")};
  }
  std::vector<std::string> errs;
  Pain002 m;
  if (auto h = root.child("GrpHdr")) {
    read_grp_hdr_base(h, m.grp_hdr, "GrpHdr", errs);
    if (auto p = h.child("InitgPty")) {
      m.grp_hdr.initg_pty.emplace();
      read_party(p, *m.grp_hdr.initg_pty);
    }
    if (auto a = h.child("FwdgAgt")) {
      m.grp_hdr.instg_agt.emplace();
      read_agent(a, *m.grp_hdr.instg_agt);
    }
  } else {
    errs.push_back("GrpHdr: required element missing");
  }
  if (auto g = root.child("OrgnlGrpInfAndSts")) {
    m.orgnl_grp_inf_and_sts.emplace();
    read_orgnl_grp_inf(g, *m.orgnl_grp_inf_and_sts, "OrgnlGrpInfAndSts", errs);
  }
  for (auto p : root.children("OrgnlPmtInfAndSts")) {
    OriginalPaymentInstruction op;
    op.orgnl_pmt_inf_id = child_text(p, "OrgnlPmtInfId");
    for (auto t : p.children("TxInfAndSts")) {
      PaymentTxStatus s;
      read_pacs002_tx_sts(t, s);
      op.txs.push_back(std::move(s));
    }
    m.pmts.push_back(std::move(op));
  }
  if (!errs.empty()) return join_errors(std::move(errs));
  return m;
}

}  // namespace stellar::inline v1::iso20022::detail
