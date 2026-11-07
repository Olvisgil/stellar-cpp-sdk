// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

/// @file pain001.cpp
/// pain.001.001.13 — CstmrCdtTrfInitn emit/read.

#include "detail/messages.hpp"

namespace stellar::inline v1::iso20022::detail {

namespace {

// GroupHeader114: MsgId, CreDtTm, Authstn?, NbOfTxs, CtrlSum?, InitgPty,
// FwdgAgt?, InitnSrc?
void emit_grp_hdr(pugi::xml_node parent, const GroupHeader& h) {
  auto n = parent.append_child("GrpHdr");
  add_el(n, "MsgId", h.msg_id);
  add_el(n, "CreDtTm", h.cre_dt_tm);
  add_el(n, "NbOfTxs", h.nb_of_txs);
  add_el_if(n, "CtrlSum", h.ctrl_sum);
  if (h.initg_pty) {
    emit_party(n, "InitgPty", *h.initg_pty);
  }
  if (h.instg_agt) emit_agent(n, "FwdgAgt", *h.instg_agt);  // instg_agt doubles as FwdgAgt here
}

void emit_tx(pugi::xml_node parent, const PainTransferTx& t) {
  auto n = parent.append_child("CdtTrfTxInf");
  emit_pmt_id6(n, t.pmt_id);
  // Amt choice: we emit InstdAmt only.
  {
    auto a = n.append_child("Amt");
    emit_amount(a, "InstdAmt", t.instd_amt);
  }
  add_el_if(n, "ChrgBr", t.chrg_br);
  if (t.ultmt_dbtr) emit_party(n, "UltmtDbtr", *t.ultmt_dbtr);
  if (t.cdtr_agt) emit_agent(n, "CdtrAgt", *t.cdtr_agt);
  if (t.cdtr_agt_acct) emit_cash_account(n, "CdtrAgtAcct", *t.cdtr_agt_acct);
  if (t.cdtr) emit_party(n, "Cdtr", *t.cdtr);
  if (t.cdtr_acct) emit_cash_account(n, "CdtrAcct", *t.cdtr_acct);
  if (t.ultmt_cdtr) emit_party(n, "UltmtCdtr", *t.ultmt_cdtr);
  if (t.purp) emit_purpose(n, *t.purp);
  if (t.rmt_inf) emit_rmt_inf(n, *t.rmt_inf);
  emit_splmtry_assets(n, t.splmtry_assets);
}

void emit_pmt_inf(pugi::xml_node parent, const PaymentInstruction& p) {
  auto n = parent.append_child("PmtInf");
  add_el(n, "PmtInfId", p.pmt_inf_id);
  add_el(n, "PmtMtd", p.pmt_mtd);
  if (p.btch_bookg) add_el(n, "BtchBookg", *p.btch_bookg ? "true" : "false");
  add_el_if(n, "NbOfTxs", p.nb_of_txs);
  add_el_if(n, "CtrlSum", p.ctrl_sum);
  {
    // ReqdExctnDt is DateAndDateTime2Choice: <ReqdExctnDt><Dt>…</Dt></ReqdExctnDt>
    auto d = n.append_child("ReqdExctnDt");
    add_el(d, "Dt", p.reqd_exctn_dt);
  }
  emit_party(n, "Dbtr", p.dbtr);
  emit_cash_account(n, "DbtrAcct", p.dbtr_acct);
  emit_agent(n, "DbtrAgt", p.dbtr_agt);
  add_el_if(n, "ChrgBr", p.chrg_br);
  for (const auto& t : p.txs) emit_tx(n, t);
}

}  // namespace

void emit_pain001(pugi::xml_node doc, const Pain001& m) {
  auto root = doc.append_child("CstmrCdtTrfInitn");
  emit_grp_hdr(root, m.grp_hdr);
  for (const auto& p : m.pmts) emit_pmt_inf(root, p);
}

Result<Pain001> read_pain001(pugi::xml_node doc) {
  auto root = doc.child("CstmrCdtTrfInitn");
  if (!root) {
    return Error{ErrorCode::kProtocolError,
                 std::string("pain.001: missing CstmrCdtTrfInitn element")};
  }
  std::vector<std::string> errs;
  Pain001 m;
  if (auto h = root.child("GrpHdr")) {
    read_grp_hdr_base(h, m.grp_hdr, "GrpHdr", errs);
    req_text(h, "NbOfTxs", m.grp_hdr.nb_of_txs, "GrpHdr", errs);
    m.grp_hdr.ctrl_sum = child_text(h, "CtrlSum");
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

  int pi = 0;
  for (auto pmt : root.children("PmtInf")) {
    const std::string path = "PmtInf[" + std::to_string(pi++) + "]";
    PaymentInstruction p;
    req_text(pmt, "PmtInfId", p.pmt_inf_id, path, errs);
    req_text(pmt, "PmtMtd", p.pmt_mtd, path, errs);
    if (auto b = pmt.child("BtchBookg")) p.btch_bookg = opt_text(b).value_or("") == "true";
    p.nb_of_txs = child_text(pmt, "NbOfTxs");
    p.ctrl_sum = child_text(pmt, "CtrlSum");
    if (auto d = pmt.child("ReqdExctnDt")) {
      p.reqd_exctn_dt = child_text(d, "Dt");
      if (p.reqd_exctn_dt.empty()) p.reqd_exctn_dt = child_text(d, "DtTm");
      if (p.reqd_exctn_dt.empty()) p.reqd_exctn_dt = opt_text(d).value_or("");
    } else {
      errs.push_back(path + "/ReqdExctnDt: required element missing");
    }
    if (auto d = pmt.child("Dbtr")) {
      read_party(d, p.dbtr);
    } else {
      errs.push_back(path + "/Dbtr: required element missing");
    }
    if (auto a = pmt.child("DbtrAcct")) {
      read_cash_account(a, p.dbtr_acct);
    } else {
      errs.push_back(path + "/DbtrAcct: required element missing");
    }
    if (auto a = pmt.child("DbtrAgt")) {
      read_agent(a, p.dbtr_agt);
    } else {
      errs.push_back(path + "/DbtrAgt: required element missing");
    }
    p.chrg_br = child_text(pmt, "ChrgBr");

    int ti = 0;
    for (auto tx : pmt.children("CdtTrfTxInf")) {
      const std::string tp = path + "/CdtTrfTxInf[" + std::to_string(ti++) + "]";
      PainTransferTx t;
      if (auto id = tx.child("PmtId")) {
        t.pmt_id.instr_id = child_text(id, "InstrId");
        req_text(id, "EndToEndId", t.pmt_id.end_to_end_id, tp + "/PmtId", errs);
        t.pmt_id.uetr = child_text(id, "UETR");
      } else {
        errs.push_back(tp + "/PmtId: required element missing");
      }
      if (auto a = tx.child("Amt")) {
        if (auto ia = a.child("InstdAmt")) {
          t.instd_amt = read_amount(ia);
        } else {
          errs.push_back(tp + "/Amt/InstdAmt: required element missing");
        }
      } else {
        errs.push_back(tp + "/Amt: required element missing");
      }
      t.chrg_br = child_text(tx, "ChrgBr");
      if (auto x = tx.child("UltmtDbtr")) {
        t.ultmt_dbtr.emplace();
        read_party(x, *t.ultmt_dbtr);
      }
      if (auto x = tx.child("CdtrAgt")) {
        t.cdtr_agt.emplace();
        read_agent(x, *t.cdtr_agt);
      }
      if (auto x = tx.child("CdtrAgtAcct")) {
        t.cdtr_agt_acct.emplace();
        read_cash_account(x, *t.cdtr_agt_acct);
      }
      if (auto x = tx.child("Cdtr")) {
        t.cdtr.emplace();
        read_party(x, *t.cdtr);
      }
      if (auto x = tx.child("CdtrAcct")) {
        t.cdtr_acct.emplace();
        read_cash_account(x, *t.cdtr_acct);
      }
      if (auto x = tx.child("UltmtCdtr")) {
        t.ultmt_cdtr.emplace();
        read_party(x, *t.ultmt_cdtr);
      }
      if (auto x = tx.child("Purp")) {
        t.purp.emplace();
        t.purp->cd = child_text(x, "Cd");
        t.purp->prtry = child_text(x, "Prtry");
      }
      if (auto r = tx.child("RmtInf")) {
        t.rmt_inf.emplace();
        read_rmt_inf(r, *t.rmt_inf);
      }
      t.splmtry_assets = read_splmtry_assets(tx);
      p.txs.push_back(std::move(t));
    }
    m.pmts.push_back(std::move(p));
  }
  if (!errs.empty()) return join_errors(std::move(errs));
  return m;
}

}  // namespace stellar::inline v1::iso20022::detail
