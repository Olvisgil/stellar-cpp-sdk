// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

// End-to-end ISO 20022 flow: an anchor runs a two-payment disbursement —
// builds the pain.001 initiation and pacs.008 FI batch, recovers the
// instructions back from the emitted XML, consumes a pacs.002/pain.002
// status report, reconciles it against the sent batch, issues a pacs.004
// return for the rejected payment, and closes the loop with a camt.053
// statement and camt.054 notification.

#include <gtest/gtest.h>

#include "stellar/core/address.hpp"
#include "stellar/core/amount.hpp"
#include "stellar/core/asset.hpp"
#include "stellar/core/memo.hpp"
#include "stellar/iso20022/payment.hpp"
#include "stellar/iso20022/stellar_map.hpp"
#include "stellar/iso20022/validate.hpp"
#include "stellar/iso20022/xml.hpp"
#include "stellar/util/clock.hpp"

using namespace stellar::v1;
using namespace stellar::v1::iso20022;

namespace {

constexpr std::string_view kAnchor = "GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ";
constexpr std::string_view kBob = "GCEZWKCA5VLDNRLN3RPRJMRZOX3Z6G5CHCGSNFHEYVXM3XOJMDS674JZ";
constexpr std::string_view kUsdcIssuer = "GA5ZSEJYB37JRC5AVCIA5MOP4RHTM335X2KGX3IHOJAPP5RE34K4KZVN";
constexpr std::string_view kHash1 = "e6e17cda8337d85e9c2b9540bd59fe8cec11d85e9c2b9540bd59fe8c";
constexpr std::string_view kHash2 =
    "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90";

Payment pay(std::string id, std::string_view hash, const char* amount) {
  Payment p;
  p.from = Address::parse(kAnchor).value();
  p.to = Address::parse(kBob).value();
  p.amount = Amount::parse(amount).value();
  p.id = std::move(id);
  p.tx_hash = hash;
  p.created_at = 1'762'500'000;
  return p;
}

CreditTransferInstruction wrap(Payment p) {
  CreditTransferInstruction i;
  i.payment = std::move(p);
  return i;
}

TEST(Iso20022E2E, DisbursementLifecycle) {
  FakeClock clock(1'762'500'000);

  // The batch: one non-ISO USDC payment (accepted) and one native XLM
  // payment (rejected downstream, then returned).
  auto p1 = pay("pay-001", kHash1, "100.1234567");
  p1.asset = Asset::credit("USDC", kUsdcIssuer).value();
  p1.memo = Memo::text("invoice 42");
  auto p2 = pay("pay-002", kHash2, "0.5");
  p2.memo = Memo::id(7);
  auto sent = std::vector<CreditTransferInstruction>{wrap(p1), wrap(p2)};

  // ── 1. customer initiation (pain.001) ──────────────────────────────
  auto pain = build_pain001(sent, {}, clock);
  ASSERT_TRUE(pain.ok()) << pain.error().describe();
  auto pain_doc = parse(*pain);
  ASSERT_TRUE(pain_doc.ok()) << pain_doc.error().describe();
  auto rep = validate(*pain_doc);
  EXPECT_TRUE(rep.valid()) << (rep.issues.empty() ? "" : rep.issues[0].message);

  auto back = instructions_from_pain001(std::get<Pain001>(*pain_doc));
  ASSERT_TRUE(back.ok()) << back.error().describe();
  ASSERT_EQ(back->size(), 2u);
  const auto& r1 = (*back)[0].payment;
  ASSERT_TRUE(r1.from && r1.to);
  EXPECT_EQ(r1.from->to_string(), kAnchor);
  EXPECT_EQ(r1.to->to_string(), kBob);
  EXPECT_FALSE(r1.asset.is_native());
  EXPECT_EQ(r1.amount.stroops(), 1001234567);  // exact 7dp via SplmtryData
  const auto& r2 = (*back)[1].payment;
  EXPECT_TRUE(r2.asset.is_native());
  EXPECT_EQ(r2.amount.stroops(), 5000000);
  EXPECT_EQ(r2.memo.type(), xdr::MemoType::MEMO_ID);

  // ── 2. FI-to-FI leg (pacs.008 batch) ───────────────────────────────
  auto pacs = build_pacs008_batch(sent, {}, clock);
  ASSERT_TRUE(pacs.ok()) << pacs.error().describe();
  auto pacs_doc = parse(*pacs);
  ASSERT_TRUE(pacs_doc.ok()) << pacs_doc.error().describe();
  const auto& p8 = std::get<Pacs008>(*pacs_doc);
  EXPECT_EQ(p8.grp_hdr.nb_of_txs, "2");
  EXPECT_EQ(p8.grp_hdr.ctrl_sum, "100.62345");
  rep = validate(*pacs_doc);
  EXPECT_TRUE(rep.valid()) << (rep.issues.empty() ? "" : rep.issues[0].message);

  auto back8 = instructions_from_pacs008(p8);
  ASSERT_TRUE(back8.ok());
  ASSERT_EQ(back8->size(), 2u);
  EXPECT_EQ((*back8)[0].payment.amount.stroops(), 1001234567);

  // ── 3. statuses come back: first ACSC, second RJCT ─────────────────
  sent[0].tx_status = TransactionStatus::kACSC;
  sent[1].tx_status = TransactionStatus::kRJCT;
  sent[1].tx_reason = "AC01";

  StatusReportOptions sr_opts;
  sr_opts.orgnl_msg_id = p8.grp_hdr.msg_id;
  sr_opts.orgnl_msg_nm_id = "pacs.008.001.14";
  auto status = build_pacs002(sent, sr_opts, clock);
  ASSERT_TRUE(status.ok()) << status.error().describe();
  auto status_doc = parse(*status);
  ASSERT_TRUE(status_doc.ok()) << status_doc.error().describe();
  rep = validate(*status_doc);
  EXPECT_TRUE(rep.valid()) << (rep.issues.empty() ? "" : rep.issues[0].message);

  auto reports = status_reports(*status_doc);
  ASSERT_TRUE(reports.ok());
  ASSERT_EQ(reports->size(), 2u);

  auto recon = reconcile_instructions(sent, *reports);
  EXPECT_TRUE(recon.unmatched.empty());
  ASSERT_EQ(recon.results.size(), 2u);
  EXPECT_TRUE(recon.results[0].matched);
  ASSERT_TRUE(recon.results[0].report);
  EXPECT_EQ(recon.results[0].report->status, "ACSC");
  EXPECT_TRUE(recon.results[1].matched);
  ASSERT_TRUE(recon.results[1].report);
  EXPECT_EQ(recon.results[1].report->status, "RJCT");
  EXPECT_EQ(recon.results[1].report->reason_code, "AC01");
  EXPECT_EQ(recon.results[1].payment_id, "pay-002");

  // the customer-side twin (pain.002) flattens the same way
  auto pain_status = build_pain002(sent, sr_opts, clock);
  ASSERT_TRUE(pain_status.ok()) << pain_status.error().describe();
  auto pain_status_doc = parse(*pain_status);
  ASSERT_TRUE(pain_status.ok());
  auto pain_reports = status_reports(*pain_status_doc);
  ASSERT_TRUE(pain_reports.ok());
  ASSERT_EQ(pain_reports->size(), 2u);
  EXPECT_EQ((*pain_reports)[1].status, "RJCT");

  // ── 4. the rejected payment is returned (pacs.004) ─────────────────
  auto returned = std::vector<CreditTransferInstruction>{sent[1]};
  returned[0].tx_status.reset();
  auto rtr = build_pacs004(returned, {}, clock);
  ASSERT_TRUE(rtr.ok()) << rtr.error().describe();
  auto rtr_doc = parse(*rtr);
  ASSERT_TRUE(rtr_doc.ok()) << rtr_doc.error().describe();
  const auto& p4 = std::get<Pacs004>(*rtr_doc);
  ASSERT_EQ(p4.txs.size(), 1u);
  EXPECT_EQ(p4.txs[0].rtrd_intrbk_sttlm_amt.ccy, "XXX");
  EXPECT_EQ(p4.txs[0].rtrd_intrbk_sttlm_amt.value, "0.5");
  EXPECT_EQ(p4.txs[0].orgnl_end_to_end_id, std::string(kHash2).substr(0, 35));
  rep = validate(*rtr_doc);
  EXPECT_TRUE(rep.valid()) << (rep.issues.empty() ? "" : rep.issues[0].message);

  // ── 5. month-end: statement + notification over the anchor account ─
  CashAccount anchor_acct;
  anchor_acct.prxy = ProxyAccount{"WALLET", std::string(kAnchor)};
  anchor_acct.ccy = "XXX";

  auto stmt = build_camt053(anchor_acct, sent, {}, clock);
  ASSERT_TRUE(stmt.ok()) << stmt.error().describe();
  auto stmt_doc = parse(*stmt);
  ASSERT_TRUE(stmt_doc.ok()) << stmt_doc.error().describe();
  const auto& c53 = std::get<Camt053>(*stmt_doc);
  ASSERT_EQ(c53.stmts.size(), 1u);
  ASSERT_EQ(c53.stmts[0].ntrys.size(), 2u);
  // anchor is the sender → both entries are debits on the statement
  for (const auto& e : c53.stmts[0].ntrys) {
    EXPECT_EQ(e.cdt_dbt_ind, "DBIT");
    EXPECT_EQ(e.sts, "BOOK");
  }
  EXPECT_EQ(c53.stmts[0].ntrys[0].ntry_ref, "pay-001");
  EXPECT_EQ(c53.stmts[0].ntrys[0].amt.value, "100.12345");  // 5dp ISO field
  ASSERT_EQ(c53.stmts[0].ntrys[0].tx_dtls.size(), 1u);
  EXPECT_EQ(c53.stmts[0].ntrys[0].tx_dtls[0].refs->end_to_end_id,
            std::string(kHash1).substr(0, 35));
  rep = validate(*stmt_doc);
  EXPECT_TRUE(rep.valid()) << (rep.issues.empty() ? "" : rep.issues[0].message);

  auto ntfctn = build_camt054(anchor_acct, sent, {}, clock);
  ASSERT_TRUE(ntfctn.ok()) << ntfctn.error().describe();
  auto ntfctn_doc = parse(*ntfctn);
  ASSERT_TRUE(ntfctn_doc.ok()) << ntfctn_doc.error().describe();
  const auto& c54 = std::get<Camt054>(*ntfctn_doc);
  ASSERT_EQ(c54.ntfctns.size(), 1u);
  EXPECT_EQ(c54.ntfctns[0].ntrys.size(), 2u);
  rep = validate(*ntfctn_doc);
  EXPECT_TRUE(rep.valid()) << (rep.issues.empty() ? "" : rep.issues[0].message);
}

}  // namespace
