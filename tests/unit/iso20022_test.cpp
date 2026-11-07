// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

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

#include <fstream>
#include <sstream>

namespace stellar::iso20022 {
namespace {

constexpr std::string_view kAlice =
    "GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ";
constexpr std::string_view kBob =
    "GCEZWKCA5VLDNRLN3RPRJMRZOX3Z6G5CHCGSNFHEYVXM3XOJMDS674JZ";
constexpr std::string_view kUsdcIssuer =
    "GA5ZSEJYB37JRC5AVCIA5MOP4RHTM335X2KGX3IHOJAPP5RE34K4KZVN";
constexpr std::string_view kTxHash =
    "e6e17cda8337d85e9c2b9540bd59fe8cec11d85e9c2b9540bd59fe8c";

std::string fixture(const char* name) {
  std::ifstream f(std::string(STELLAR_ISO20022_FIXTURES_DIR) + "/" + name);
  EXPECT_TRUE(f.good()) << "missing fixture " << name;
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

Address addr(std::string_view s) { return Address::parse(s).value(); }

Payment make_payment() {
  Payment p;
  p.from = addr(kAlice);
  p.to = addr(kBob);
  p.asset = Asset::native();
  p.amount = Amount::parse("100.1234567").value();
  p.id = "pay-001";
  p.tx_hash = kTxHash;
  p.created_at = 1'762'500'000;  // 2025-11-02T…
  return p;
}

CreditTransferInstruction instr_of(const Payment& p) {
  CreditTransferInstruction i;
  i.payment = p;
  return i;
}

std::vector<CreditTransferInstruction> batch_of(const Payment& p) {
  return {instr_of(p)};
}

TEST(Iso20022Validate, Identifiers) {
  EXPECT_TRUE(is_bic("DEUTDEFF"));
  EXPECT_TRUE(is_bic("DEUTDEFF500"));
  EXPECT_FALSE(is_bic("DEUTDEFF5"));
  EXPECT_TRUE(is_iban("DE89370400440532013000"));
  EXPECT_FALSE(is_iban("DE89370400440532013001"));
  EXPECT_TRUE(is_iso4217("EUR"));
  EXPECT_TRUE(is_iso4217("XXX"));
  EXPECT_FALSE(is_iso4217("XLM"));
  EXPECT_FALSE(is_iso4217("USDC"));
  EXPECT_TRUE(is_iso3166("DE"));
  EXPECT_FALSE(is_iso3166("XX"));
  EXPECT_TRUE(is_uetr("f47ac10b-58cc-4372-a567-0e02b2c3d479"));
  EXPECT_FALSE(is_uetr("f47ac10b58cc4372a5670e02b2c3d479"));
}

TEST(Iso20022Parse, DetectAndFields) {
  const auto xml = fixture("pacs008.xml");
  ASSERT_EQ(detect_message_type(xml).value(), MessageKind::kPacs008);
  auto doc_r = parse(xml);
  ASSERT_TRUE(doc_r.ok()) << doc_r.error().describe();
  const auto& doc = std::get<Pacs008>(*doc_r);
  EXPECT_EQ(doc.grp_hdr.msg_id, "PACS008TEST001");
  EXPECT_EQ(doc.grp_hdr.nb_of_txs, "1");
  ASSERT_EQ(doc.txs.size(), 1u);
  const auto& t = doc.txs.front();
  EXPECT_EQ(t.pmt_id.end_to_end_id, "E2E-INV-0001");
  EXPECT_EQ(t.pmt_id.uetr, "f47ac10b-58cc-4372-a567-0e02b2c3d479");
  EXPECT_EQ(t.intrbk_sttlm_amt.ccy, "EUR");
  EXPECT_EQ(t.intrbk_sttlm_amt.value, "25000");
  EXPECT_EQ(t.dbtr.nm, "Acme Corp GmbH");
  ASSERT_TRUE(t.dbtr_acct && t.dbtr_acct->id);
  EXPECT_EQ(t.dbtr_acct->id->iban, "DE89370400440532013000");
  EXPECT_EQ(t.dbtr_agt.bicfi, "DEUTDEFF");
  ASSERT_TRUE(t.rmt_inf);
  ASSERT_EQ(t.rmt_inf->ustrd.size(), 1u);
  EXPECT_EQ(t.rmt_inf->ustrd[0], "Invoice INV-2026-001");
}

TEST(Iso20022Parse, RoundTripPreservesDocument) {
  for (const char* name : {"pacs008.xml", "pacs002.xml", "pain001.xml", "camt053.xml"}) {
    const auto xml = fixture(name);
    auto first = parse(xml);
    ASSERT_TRUE(first.ok()) << name << ": " << first.error().describe();
    auto emitted = to_xml(*first);
    ASSERT_TRUE(emitted.ok()) << name;
    auto second = parse(*emitted);
    ASSERT_TRUE(second.ok()) << name << ": " << second.error().describe();
    EXPECT_EQ(*first, *second) << name;
  }
}

TEST(Iso20022Parse, RejectsDoctypeAndBadNamespace) {
  EXPECT_FALSE(parse("<!DOCTYPE x><Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.14\"/>").ok());
  EXPECT_FALSE(parse("<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.08\"/>").ok());
  EXPECT_FALSE(parse("<NotADocument/>").ok());
  auto r = parse("<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.14\"><FIToFICstmrCdtTrf><CdtTrfTxInf/></FIToFICstmrCdtTrf></Document>");
  ASSERT_FALSE(r.ok());
  EXPECT_NE(r.error().message().find("GrpHdr"), std::string::npos);
  EXPECT_NE(r.error().message().find("PmtId"), std::string::npos);
}

TEST(Iso20022Build, Pacs008SinglePaymentConventions) {
  FakeClock clock(1'762'500'000);
  auto p = make_payment();
  p.memo = Memo::text("invoice 42");
  p.asset = Asset::credit("USDC", kUsdcIssuer).value();

  auto xml = build_pacs008(p, {}, clock);
  ASSERT_TRUE(xml.ok()) << xml.error().describe();
  const auto& out = *xml;

  // non-ISO asset → Ccy="XXX" + SplmtryData asset block
  EXPECT_NE(out.find("IntrBkSttlmAmt Ccy=\"XXX\""), std::string::npos);
  EXPECT_NE(out.find("<SplmtryData>"), std::string::npos);
  EXPECT_NE(out.find("<Cd>USDC</Cd>"), std::string::npos);
  EXPECT_NE(out.find("<Amt>100.1234567</Amt>"), std::string::npos);
  // 7dp truncated to 5 in the ISO amount
  EXPECT_NE(out.find(">100.12345</IntrBkSttlmAmt>"), std::string::npos);
  // strkeys land in Prxy/Id (56 chars > Max34Text)
  EXPECT_NE(out.find("<Prxy>"), std::string::npos);
  EXPECT_NE(out.find(kBob), std::string::npos);
  // tx hash → EndToEndId (≤35), memo → Ustrd
  EXPECT_NE(out.find("<EndToEndId>" + std::string(kTxHash).substr(0, 35) + "</EndToEndId>"),
            std::string::npos);
  EXPECT_NE(out.find("<Ustrd>invoice 42</Ustrd>"), std::string::npos);

  // and it parses back
  auto doc = parse(out);
  ASSERT_TRUE(doc.ok()) << doc.error().describe();
  const auto& p8 = std::get<Pacs008>(*doc);
  EXPECT_EQ(p8.txs.front().intrbk_sttlm_amt.ccy, "XXX");
  ASSERT_EQ(p8.txs.front().splmtry_assets.size(), 1u);
  EXPECT_EQ(p8.txs.front().splmtry_assets[0].cd, "USDC");

  auto rep = validate(*doc);
  EXPECT_TRUE(rep.valid()) << (rep.issues.empty() ? "" : rep.issues[0].path + ": " + rep.issues[0].message);
}

TEST(Iso20022Build, Pacs008BatchTotals) {
  FakeClock clock(1'762'500'000);
  auto p1 = make_payment();
  auto p2 = make_payment();
  p2.id = "pay-002";
  p2.amount = Amount::parse("0.5").value();
  const auto instrs = std::vector<CreditTransferInstruction>{instr_of(p1), instr_of(p2)};

  auto xml = build_pacs008_batch(instrs, {}, clock);
  ASSERT_TRUE(xml.ok()) << xml.error().describe();
  auto doc = parse(*xml);
  ASSERT_TRUE(doc.ok());
  const auto& p8 = std::get<Pacs008>(*doc);
  EXPECT_EQ(p8.grp_hdr.nb_of_txs, "2");
  EXPECT_EQ(p8.grp_hdr.ctrl_sum, "100.62345");  // 100.12345 + 0.5
  ASSERT_TRUE(p8.grp_hdr.ttl_intrbk_sttlm_amt);
  EXPECT_EQ(p8.grp_hdr.ttl_intrbk_sttlm_amt->ccy, "XXX");
}

TEST(Iso20022Build, InstructionsRoundTrip) {
  FakeClock clock(1'762'500'000);
  auto p = make_payment();
  p.memo = Memo::id(42);
  auto xml = build_pacs008(p, {}, clock);
  ASSERT_TRUE(xml.ok());
  auto doc = parse(*xml);
  ASSERT_TRUE(doc.ok());
  auto instrs = instructions_from_pacs008(std::get<Pacs008>(*doc));
  ASSERT_TRUE(instrs.ok());
  ASSERT_EQ(instrs->size(), 1u);
  const auto& back = instrs->front().payment;
  ASSERT_TRUE(back.from && back.to);
  EXPECT_EQ(back.from->to_string(), kAlice);
  EXPECT_EQ(back.to->to_string(), kBob);
  EXPECT_TRUE(back.asset.is_native());
  // exact 7-dp amount restored from SplmtryData, not the 5dp ISO field
  EXPECT_EQ(back.amount.stroops(), 1001234567);
  EXPECT_EQ(back.memo.type(), xdr::MemoType::MEMO_ID);
}

TEST(Iso20022Build, Pain001Disbursement) {
  FakeClock clock(1'762'500'000);
  auto instrs = batch_of(make_payment());
  TransferBatchOptions opts;
  opts.reqd_exctn_dt = "2025-11-03";
  auto xml = build_pain001(instrs, opts, clock);
  ASSERT_TRUE(xml.ok()) << xml.error().describe();
  EXPECT_NE(xml->find("<CstmrCdtTrfInitn>"), std::string::npos);
  auto doc = parse(*xml);
  ASSERT_TRUE(doc.ok()) << doc.error().describe();
  const auto& p1 = std::get<Pain001>(*doc);
  // ReqdExctnDt is a DateAndDateTime2Choice — must wrap the date in <Dt>
  EXPECT_NE(xml->find("<ReqdExctnDt>"), std::string::npos);
  EXPECT_NE(xml->find("<Dt>2025-11-03</Dt>"), std::string::npos);
  ASSERT_EQ(p1.pmts.size(), 1u);
  EXPECT_EQ(p1.pmts[0].pmt_mtd, "TRF");
  EXPECT_EQ(p1.pmts[0].reqd_exctn_dt, "2025-11-03");
  ASSERT_TRUE(p1.grp_hdr.initg_pty);
  EXPECT_TRUE(validate(*doc).valid());
}

TEST(Iso20022Build, Pacs002StatusAndReconcile) {
  FakeClock clock(1'762'500'000);
  auto p = make_payment();
  auto ok_i = instr_of(p);
  auto bad = make_payment();
  bad.id = "pay-002";
  bad.tx_hash = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4";
  auto bad_i = instr_of(bad);
  bad_i.tx_status = TransactionStatus::kRJCT;
  bad_i.tx_reason = "NO_TRUST";
  const auto instrs = std::vector<CreditTransferInstruction>{ok_i, bad_i};

  StatusReportOptions opts;
  opts.orgnl_msg_id = "BATCH-1";
  auto xml = build_pacs002(instrs, opts, clock);
  ASSERT_TRUE(xml.ok()) << xml.error().describe();
  auto doc = parse(*xml);
  ASSERT_TRUE(doc.ok()) << doc.error().describe();

  auto reports = status_reports(*doc);
  ASSERT_TRUE(reports.ok());
  ASSERT_EQ(reports->size(), 2u);
  EXPECT_EQ((*reports)[0].status, "ACSC");
  EXPECT_EQ((*reports)[0].original_msg_id, "BATCH-1");
  EXPECT_EQ((*reports)[1].status, "RJCT");
  EXPECT_EQ((*reports)[1].reason_code, "NO_TRUST");

  auto sum = reconcile_instructions(instrs, *reports);
  EXPECT_TRUE(sum.results[0].matched);
  EXPECT_TRUE(sum.results[1].matched);
  EXPECT_TRUE(sum.unmatched.empty());
}

TEST(Iso20022Build, Camt053StatementEntries) {
  FakeClock clock(1'762'500'000);
  CashAccount acct = [] {
    CashAccount a;
    a.prxy = ProxyAccount{"WALLET", std::string(kBob)};
    return a;
  }();
  auto instrs = batch_of(make_payment());
  auto xml = build_camt053(acct, instrs, {}, clock);
  ASSERT_TRUE(xml.ok()) << xml.error().describe();
  auto doc = parse(*xml);
  ASSERT_TRUE(doc.ok()) << doc.error().describe();
  const auto& c53 = std::get<Camt053>(*doc);
  ASSERT_EQ(c53.stmts.size(), 1u);
  ASSERT_EQ(c53.stmts[0].ntrys.size(), 1u);
  const auto& e = c53.stmts[0].ntrys[0];
  EXPECT_EQ(e.cdt_dbt_ind, "CRDT");  // Bob is the account owner → credit
  EXPECT_EQ(e.sts, "BOOK");
  ASSERT_EQ(e.tx_dtls.size(), 1u);
  EXPECT_EQ(e.tx_dtls[0].refs->end_to_end_id, std::string(kTxHash).substr(0, 35));
}

TEST(Iso20022Validate, ReportCollectsIssues) {
  auto xml = fixture("pacs008.xml");
  auto doc = parse(xml);
  ASSERT_TRUE(doc.ok());
  EXPECT_TRUE(validate(*doc).valid());

  Pacs008 bad;
  bad.grp_hdr.msg_id = "X";
  bad.grp_hdr.cre_dt_tm = "now";
  bad.grp_hdr.nb_of_txs = "1";
  bad.grp_hdr.sttlm_inf = SettlementInstruction{"CLRG"};
  CreditTransferTx t;
  t.pmt_id.end_to_end_id = "";  // missing → issue
  t.intrbk_sttlm_amt = {"USDC", "1.123456"};  // bad ccy + too many digits
  bad.txs.push_back(t);
  const auto rep = validate(Document{bad});
  EXPECT_FALSE(rep.valid());
  EXPECT_GE(rep.issues.size(), 3u);
  EXPECT_EQ(rep.issues[0].path, "CdtTrfTxInf[0]/PmtId/EndToEndId");
}

}  // namespace
}  // namespace stellar::iso20022
