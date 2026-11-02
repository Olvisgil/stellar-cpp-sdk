// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
// Live-network integration test against a real Stellar RPC endpoint.
// Skips unless STELLAR_RPC_ENDPOINT is set (e.g. https://soroban-testnet.stellar.org).
#include <fstream>
#include "stellar/contract/contract_client.hpp"
#include "stellar/contract/contract_meta.hpp"
#include "stellar/contract/scval.hpp"
#include "stellar/core/asset.hpp"
#include "stellar/http.hpp"
#include "stellar/rpc/rpc_client.hpp"
#include "stellar/tx/ops.hpp"
#include "stellar/xdr/codec.hpp"

#include <cstdio>
#include <cstdlib>

#include <gtest/gtest.h>

using namespace stellar::v1;

static const char* endpoint() {
  return std::getenv("STELLAR_RPC_ENDPOINT");
}

class RpcLive : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!endpoint() || std::string(endpoint()).empty()) {
      GTEST_SKIP() << "set STELLAR_RPC_ENDPOINT to run live RPC tests";
    }
  }
};

TEST_F(RpcLive, HealthAndLedger) {
  CurlTransport http;
  RpcClient rpc(http, endpoint());
  auto h = rpc.get_health();
  ASSERT_TRUE(h.ok()) << h.error().describe();
  EXPECT_TRUE(*h);

  auto seq = rpc.get_latest_ledger_sequence();
  ASSERT_TRUE(seq.ok()) << seq.error().describe();
  EXPECT_GT(*seq, 5'000'000u);

  auto net = rpc.get_network();
  ASSERT_TRUE(net.ok());
  EXPECT_TRUE(net->at("friendbotUrl").is_string() || net->at("passphrase").is_string());
}

TEST_F(RpcLive, RecentLedgers) {
  CurlTransport http;
  RpcClient rpc(http, endpoint());
  auto seq = rpc.get_latest_ledger_sequence();
  ASSERT_TRUE(seq.ok());
  auto res = rpc.get_ledgers(*seq - 1, 2);
  ASSERT_TRUE(res.ok()) << res.error().describe();
  ASSERT_GE(res->ledgers.size(), 1u);
  const auto& row = res->ledgers.back();
  EXPECT_EQ(row.sequence, *seq);
  EXPECT_EQ(row.hash.size(), 64u);
  // LedgerCloseMeta decodes into a real protocol type
  EXPECT_TRUE(row.meta.v() >= 0);
}

TEST_F(RpcLive, VersionAndFees) {
  CurlTransport http;
  RpcClient rpc(http, endpoint());
  auto v = rpc.get_version_info();
  ASSERT_TRUE(v.ok()) << v.error().describe();
  EXPECT_FALSE(v->version.empty());

  auto f = rpc.get_fee_stats();
  ASSERT_TRUE(f.ok()) << f.error().describe();
  EXPECT_GE(f->inclusion.p90, f->inclusion.min);
  EXPECT_GT(f->inclusion.min, 0u);
}

// End-to-end: fund via friendbot, invoke the native Stellar Asset Contract's
// `transfer` on the live network, poll for the result.
TEST_F(RpcLive, ContractInvokeEndToEnd) {
  CurlTransport http;
  RpcClient rpc(http, endpoint());
  auto net = rpc.get_network();
  ASSERT_TRUE(net.ok());
  const std::string friendbot =
      net->at("friendbotUrl").is_string() ? net->at("friendbotUrl").as_string() : "";
  if (friendbot.empty()) GTEST_SKIP() << "endpoint has no friendbot";

  // network passphrase from the endpoint — works on any network, not just testnet
  ASSERT_TRUE(net->at("passphrase").is_string());
  const Network net_live{net->at("passphrase").as_string()};

  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());
  auto src = Address::parse(kp->account_id());
  ASSERT_TRUE(src.ok());

  HttpRequest fr;
  fr.method = "GET";
  fr.url = friendbot + "?addr=" + kp->account_id();
  auto fres = http.perform(fr);
  ASSERT_TRUE(fres.ok() && fres->status == 200) << "friendbot funding failed";

  // fetch the new account's sequence number
  xdr::LedgerKey key = xdr::LedgerKey::account(
      xdr::LedgerKeyAccount{xdr::PublicKey::publicKeyTypeEd25519(xdr::uint256{kp->public_key()})});
  auto entries = rpc.get_ledger_entries({key});
  ASSERT_TRUE(entries.ok()) << entries.error().describe();
  ASSERT_EQ(entries->entries.size(), 1u);
  const auto seq = entries->entries[0].data.account().seqNum + 1;

  // native SAC contract id on this network
  auto sac = Asset::native().sac_contract_id(net_live);
  ASSERT_TRUE(sac.ok());
  ContractClient cc(rpc, net_live, contract::contract_strkey(*sac));

  // read-only call first: symbol() -> "native"
  auto sym = cc.call(*src, "symbol", {});
  ASSERT_TRUE(sym.ok()) << sym.error().describe();
  ASSERT_NE(scval_get::string(*sym), nullptr);

  // write call: transfer 1 stroop to ourselves (source-account auth)
  KeyPairSigner signer(std::move(*kp));
  // full invoke through the library API
  ContractClient::InvokeOptions io;
  auto res = cc.invoke(*src, seq, signer, "transfer",
                       {scval::address(*src), scval::address(*src), scval::i128(0, 1)});
  ASSERT_TRUE(res.ok()) << res.error().describe();
  EXPECT_EQ(res->status, "SUCCESS");
}

// ---------------------------------------------------------------------------
// Vendored WASM deploy + spec-driven invoke — end-to-end on testnet
// ---------------------------------------------------------------------------
TEST_F(RpcLive, VendoredWasmDeployInvoke) {
  const char* endpoint = std::getenv("STELLAR_RPC_ENDPOINT");
  if (!endpoint) GTEST_SKIP() << "STELLAR_RPC_ENDPOINT not set";
  CurlTransport http;
  RpcClient rpc(http, endpoint);
  const auto net{Network::testnet()};

  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());
  auto src = Address::parse(kp->account_id());
  ASSERT_TRUE(src.ok());
  const std::string friendbot = net == Network::testnet() ? "https://friendbot.stellar.org"
                                                          : "http://localhost:8000/friendbot";
  HttpRequest fr;
  fr.method = "GET";
  fr.url = friendbot + "?addr=" + kp->account_id();
  ASSERT_TRUE(http.perform(fr).ok());

  xdr::LedgerKey key = xdr::LedgerKey::account(
      xdr::LedgerKeyAccount{xdr::PublicKey::publicKeyTypeEd25519(xdr::uint256{kp->public_key()})});
  auto entries = rpc.get_ledger_entries({key});
  ASSERT_TRUE(entries.ok());
  auto seq = entries->entries[0].data.account().seqNum + 1;
  KeyPairSigner signer(std::move(*kp));

  // read the vendored wasm
  std::ifstream wf(std::string(STELLAR_FIXTURES_DIR) + "/../wasm/hello_world.wasm",
                   std::ios::binary);
  ASSERT_TRUE(wf.good());
  const Bytes wasm{std::istreambuf_iterator<char>(wf), {}};

  // 1) upload wasm
  auto upload_fn = contract::upload_wasm_fn(ByteSpan(wasm.data(), wasm.size()));
  auto upload_res =
      ContractClient(rpc, net, "C" + std::string(56 - 1, 'A'))  // id unused for upload
          .invoke_host_function(*src, seq, signer, upload_fn);
  ASSERT_TRUE(upload_res.ok()) << upload_res.error().describe();
  ASSERT_EQ(upload_res->status, "SUCCESS");
  ++seq;
  const auto wasm_hash = sha256(ByteSpan(wasm.data(), wasm.size()));

  // 2) deploy contract v2 (no constructor args)
  std::array<std::uint8_t, 32> salt{};
  salt.fill(0x42);  // deterministic salt — contract id is derived from it
  auto create_fn =
      contract::create_contract_v2_fn(*src, salt, contract::wasm_executable(wasm_hash), {});
  ASSERT_TRUE(create_fn.ok()) << create_fn.error().describe();
  auto create_res = ContractClient(rpc, net, "C" + std::string(56 - 1, 'A'))
                        .invoke_host_function(*src, seq, signer, *create_fn);
  ASSERT_TRUE(create_res.ok()) << create_res.error().describe();
  ASSERT_EQ(create_res->status, "SUCCESS");
  ++seq;

  // contract id = id_from_address(deployer, salt)
  auto cid = contract::id_from_address(net, *src, salt);
  ASSERT_TRUE(cid.ok());
  ContractClient cc(rpc, net, contract::contract_strkey(*cid));

  // 3) attach the decoded spec from the same wasm, then spec-driven call
  auto meta = parse_contract_meta(ByteSpan(wasm.data(), wasm.size()));
  ASSERT_TRUE(meta.ok());
  auto spec =
      ContractSpec::decode(ByteSpan(meta->spec_entries_xdr.data(), meta->spec_entries_xdr.size()));
  ASSERT_TRUE(spec.ok());
  cc.set_spec(*spec);

  auto hello = cc.call_json(*src, "hello", Json::parse(R"({"to": "world"})").value());
  ASSERT_TRUE(hello.ok()) << hello.error().describe();
  // hello returns Vec<String> = ["Hello", "world"]
  const auto* vec = scval_get::vec(*hello);
  ASSERT_NE(vec, nullptr);
  ASSERT_EQ(vec->size(), 2u);
  EXPECT_EQ(vec->at(0).str(), "Hello");
  EXPECT_EQ(vec->at(1).str(), "world");
}
