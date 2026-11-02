// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#include "stellar/core/asset.hpp"
#include "stellar/json.hpp"
#include "stellar/rpc/rpc_client.hpp"
#include "stellar/util/clock.hpp"

#include <fstream>

#include <gtest/gtest.h>

using namespace stellar::v1;

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

TEST(Json, ParseRoundTrip) {
  auto j = Json::parse(R"({"a":1,"b":[true,null,"x\ny"],"c":{"d":-2.5}})");
  ASSERT_TRUE(j.ok());
  EXPECT_EQ(j->at("a").as_int(), 1);
  EXPECT_TRUE(j->at("b").at(0).as_bool());
  EXPECT_TRUE(j->at("b").at(1).is_null());
  EXPECT_EQ(j->at("b").at(2).as_string(), "x\ny");
  EXPECT_DOUBLE_EQ(j->at("c").at("d").as_double(), -2.5);
  EXPECT_TRUE(j->at("missing").is_null());

  // canonical re-serialization parses identically
  auto j2 = Json::parse(j->dump());
  ASSERT_TRUE(j2.ok());
  EXPECT_EQ(*j2, *j);
}

TEST(Json, RejectsBad) {
  for (const char* s : {"{", "[1,]", "{\"a\":1,}", "\"\\x\"", "01", "nul", "{\"a\":1,\"a\":2}",
                        "[1 2]", "{\"a\"}"}) {
    EXPECT_FALSE(Json::parse(s).ok()) << "accepted " << s;
  }
}

TEST(Json, Builder) {
  Json o = Json::object();
  o.set("k", 5);
  o.set("s", "v");
  Json arr = Json::array();
  arr.push(1);
  arr.push("two");
  o.set("arr", arr);
  EXPECT_EQ(o.dump(), R"({"arr":[1,"two"],"k":5,"s":"v"})");
}

// ---------------------------------------------------------------------------
// RPC client over a mock transport
// ---------------------------------------------------------------------------

class ScriptMockTransport final : public HttpTransport {
 public:
  std::vector<std::pair<HttpRequest, HttpResponse>> script;
  std::vector<HttpRequest> requests;
  std::size_t idx = 0;

  Result<HttpResponse> perform(const HttpRequest& req) override {
    requests.push_back(req);
    if (idx >= script.size()) {
      return Error{ErrorCode::kNetwork, "mock: no more scripted responses"};
    }
    return script[idx++].second;
  }
};

static HttpResponse ok_json(std::string body) {
  HttpResponse r;
  r.status = 200;
  r.body = std::move(body);
  return r;
}

TEST(Rpc, RequestShape) {
  ScriptMockTransport mt;
  mt.script.push_back({{}, ok_json(R"({"jsonrpc":"2.0","id":1,"result":{"status":"healthy"}})")});
  RpcClient rpc(mt, "https://rpc.test/");
  auto h = rpc.get_health();
  ASSERT_TRUE(h.ok());
  EXPECT_TRUE(*h);

  // request shape: POST, JSON body with method + id
  ASSERT_EQ(mt.requests.size(), 1u);
  auto doc = Json::parse(mt.requests[0].body);
  ASSERT_TRUE(doc.ok());
  EXPECT_EQ(doc->at("method").as_string(), "getHealth");
  EXPECT_EQ(doc->at("jsonrpc").as_string(), "2.0");
  EXPECT_TRUE(doc->at("id").is_number());
  EXPECT_EQ(mt.requests[0].headers.at("Content-Type"), "application/json");
}

TEST(Rpc, ErrorResponse) {
  ScriptMockTransport mt;
  mt.script.push_back(
      {{}, ok_json(R"({"jsonrpc":"2.0","id":1,"error":{"code":-32602,"message":"bad params"}})")});
  RpcClient rpc(mt, "https://rpc.test/");
  auto h = rpc.get_health();
  EXPECT_FALSE(h.ok());
  EXPECT_EQ(h.error().code(), stellar::v1::ErrorCode::kRpcError);
}

TEST(Rpc, HttpFailure) {
  ScriptMockTransport mt;
  HttpResponse res;
  res.status = 503;
  mt.script.push_back({{}, res});
  RpcClient rpc(mt, "https://rpc.test/");
  EXPECT_FALSE(rpc.get_health().ok());
}

TEST(Rpc, TransactionParsing) {
  // a real testnet envelope fixture for the envelopeXdr field
  std::ifstream f(std::string(STELLAR_FIXTURES_DIR) + "/tx_envelope_9d4c3558efd4.b64");
  std::string env_b64;
  std::getline(f, env_b64);
  auto env_bytes = base64_decode(env_b64);
  ASSERT_TRUE(env_bytes.ok());
  xdr::TransactionResult txr;
  txr.feeCharged = 100;
  txr.result = xdr::TransactionResultResult::txsuccess({});
  txr.ext = xdr::TransactionResultExt::v0();
  auto txr_bytes = xdr::encode(txr);
  ASSERT_TRUE(txr_bytes.ok());
  const std::string result_b64 = base64_encode(*txr_bytes);

  const std::string body =
      std::string(
          R"({"jsonrpc":"2.0","id":1,"result":{"status":"SUCCESS","ledger":5509236,"applicationOrder":1,)") +
      "\"txHash\":\"9d4c3558efd4\",\"envelopeXdr\":\"" + env_b64 + "\",\"resultXdr\":\"" +
      result_b64 + "\"}}";
  ScriptMockTransport mt;
  mt.script.push_back({{}, ok_json(body)});
  RpcClient rpc(mt, "https://rpc.test/");
  auto t = rpc.get_transaction("9d4c3558efd4");
  ASSERT_TRUE(t.ok()) << t.error().describe();
  EXPECT_TRUE(t->found);
  EXPECT_EQ(t->status, "SUCCESS");
  EXPECT_EQ(t->ledger, 5509236u);
  ASSERT_TRUE(t->envelope.has_value());
}

TEST(Rpc, LedgerEntriesDecodes) {
  // encode a real LedgerKey + LedgerEntry for the mock
  auto usd = Asset::parse("USD:GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");
  ASSERT_TRUE(usd.ok());
  xdr::LedgerKey key = xdr::LedgerKey::account(
      xdr::LedgerKeyAccount{xdr::PublicKey::publicKeyTypeEd25519(xdr::uint256{})});
  auto key_bytes = xdr::encode(key);
  ASSERT_TRUE(key_bytes.ok());
  const std::string key_b64 = base64_encode(*key_bytes);

  xdr::LedgerEntry ent;
  ent.lastModifiedLedgerSeq = 5509236;
  ent.data = xdr::LedgerEntryData::account(xdr::AccountEntry{});
  auto ent_bytes = xdr::encode(ent);
  ASSERT_TRUE(ent_bytes.ok());

  const std::string body =
      std::string(R"({"jsonrpc":"2.0","id":1,"result":{"entries":[{"keyXdr":")") + key_b64 +
      R"(","xdr":")" + base64_encode(*ent_bytes) +
      R"(","lastModifiedLedgerSeq":5509236}],"latestLedger":5509239}})";
  ScriptMockTransport mt;
  mt.script.push_back({{}, ok_json(body)});
  RpcClient rpc(mt, "https://rpc.test/");
  auto res = rpc.get_ledger_entries({key});
  ASSERT_TRUE(res.ok()) << res.error().describe();
  ASSERT_EQ(res->entries.size(), 1u);
  EXPECT_EQ(res->latest_ledger, 5509239u);
  EXPECT_EQ(res->entries[0].last_modified_ledger, 5509236u);
  EXPECT_EQ(res->entries[0].data.type(), xdr::LedgerEntryType::ACCOUNT);

  // request params carry base64'd keys
  auto doc = Json::parse(mt.requests[0].body);
  ASSERT_TRUE(doc.ok());
  EXPECT_EQ(doc->at("method").as_string(), "getLedgerEntries");
  EXPECT_EQ(doc->at("params").at("keys").at(0).as_string(), key_b64);
}

TEST(Rpc, SendTransaction) {
  std::ifstream f(std::string(STELLAR_FIXTURES_DIR) + "/tx_envelope_9d4c3558efd4.b64");
  std::string env_b64;
  std::getline(f, env_b64);
  auto env = TransactionEnvelope::from_base64(env_b64);
  ASSERT_TRUE(env.ok());

  ScriptMockTransport mt;
  mt.script.push_back(
      {{}, ok_json(R"({"jsonrpc":"2.0","id":1,"result":{"status":"PENDING","hash":"deadbeef"}})")});
  RpcClient rpc(mt, "https://rpc.test/");
  auto res = rpc.send_transaction(*env);
  ASSERT_TRUE(res.ok());
  EXPECT_EQ(res->status, "PENDING");
  EXPECT_EQ(res->hash, "deadbeef");

  auto doc = Json::parse(mt.requests[0].body);
  ASSERT_TRUE(doc.ok());
  EXPECT_EQ(doc->at("method").as_string(), "sendTransaction");
  EXPECT_EQ(doc->at("params").at("transaction").as_string(), env_b64);
}

// ---------------------------------------------------------------------------
// Phase 0/4 infrastructure: Clock/RandomSource/Logger, MockTransport,
// multipart/SSE, lazy decode, DUPLICATE
// ---------------------------------------------------------------------------

TEST(Clock, FakeClockAdvances) {
  FakeClock c(1000);
  EXPECT_EQ(c.now_seconds(), 1000);
  c.advance(60);
  EXPECT_EQ(c.now_seconds(), 1060);
  EXPECT_EQ(c.monotonic_seconds(), 60);
}

TEST(RandomSource, Deterministic) {
  DeterministicRandom r(0x42);
  const auto a = r.bytes(4), b = r.bytes(4);
  EXPECT_EQ(a.size(), 4u);
  EXPECT_NE(a, b);  // stream advances
}

TEST(Logger, Redact) {
  EXPECT_EQ(redact("sign SAB3CUBQUCQC7XWLNYT7GX2H5WDKV3B2NQ3EPLK3VJZ3KHZF4BQZ5E7J end"),
            "sign <REDACTED> end");
  EXPECT_EQ(redact("token eyJhbGciOiJIUzI1NiJ9.payload.sig"), "token <REDACTED>");
}

TEST(Http, MockTransportPublic) {
  MockTransport mt;
  mt.add_response("/health", {200, R"({"ok":1})", {}});
  mt.add_error("/fail", Error::invalid_argument("no route"));
  HttpRequest req;
  req.url = "https://x/health";
  req.method = "GET";
  auto r = mt.perform(req);
  ASSERT_TRUE(r.ok() && r->status == 200);
  req.url = "https://x/fail";
  EXPECT_FALSE(mt.perform(req).ok());
  req.url = "https://x/unknown";
  EXPECT_FALSE(mt.perform(req).ok());
  EXPECT_EQ(mt.requests().size(), 3u);
}

TEST(Http, MultipartSse) {
  MultipartEncoder mp;
  mp.add_field("name", "val");
  mp.add_file("f", "a.bin", ByteSpan(reinterpret_cast<const std::uint8_t*>("data"), 4));
  EXPECT_TRUE(mp.body().find("name=\"name\"") != std::string::npos);
  EXPECT_TRUE(mp.body().find("data") != std::string::npos);
  EXPECT_TRUE(mp.content_type().find("multipart/form-data") != std::string::npos);

  SseParser sse;
  auto frames = sse.feed("event: msg\ndata: one\n\n");
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].event, "msg");
  EXPECT_EQ(frames[0].data, "one");
  auto frames2 = sse.feed("data: two\n");
  ASSERT_TRUE(frames2.empty());  // incomplete
  auto frames3 = sse.feed("data: three\n\n");
  ASSERT_EQ(frames3.size(), 1u);
  EXPECT_EQ(frames3[0].data, "two\nthree");
}
