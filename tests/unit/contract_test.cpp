// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
#include "stellar/contract/contract_client.hpp"
#include "stellar/contract/contract_meta.hpp"
#include "stellar/contract/delegate_tree.hpp"
#include <fstream>
#include "stellar/contract/scval.hpp"
#include "stellar/tx/ops.hpp"

#include <gtest/gtest.h>

using namespace stellar::v1;

TEST(ScVal, ConstructAndRead) {
  EXPECT_EQ(*scval_get::u32(scval::u32(42)), 42u);
  EXPECT_EQ(*scval_get::i32(scval::i32(-7)), -7);
  EXPECT_EQ(*scval_get::i64(scval::i64(-9)), -9LL);
  EXPECT_TRUE(*scval_get::bool_(scval::bool_(true)));
  EXPECT_EQ(*scval_get::u64(scval::u64(99)), 99ULL);

  auto s = scval::string("hello");
  ASSERT_NE(scval_get::string(s), nullptr);
  EXPECT_EQ(std::string(scval_get::string(s)->begin(), scval_get::string(s)->end()), "hello");

  auto sym = scval::symbol("sym");
  ASSERT_NE(scval_get::symbol(sym), nullptr);
  EXPECT_EQ(std::string(scval_get::symbol(sym)->begin(), scval_get::symbol(sym)->end()), "sym");

  auto b = scval::bytes(ByteSpan{Bytes{0xde, 0xad}});
  ASSERT_NE(scval_get::bytes(b), nullptr);
  EXPECT_EQ(*scval_get::bytes(b), (Bytes{0xde, 0xad}));

  // mismatched reads return null
  EXPECT_EQ(scval_get::u32(s), nullptr);
  EXPECT_EQ(scval_get::string(b), nullptr);
}

TEST(ScVal, VecMapRoundTrip) {
  auto v = scval::vec({scval::u32(1), scval::symbol("two"), scval::bool_(false)});
  const auto* pv = scval_get::vec(v);
  ASSERT_NE(pv, nullptr);
  ASSERT_EQ(pv->size(), 3u);
  EXPECT_EQ(*scval_get::u32((*pv)[0]), 1u);
  EXPECT_EQ(std::string(scval_get::symbol((*pv)[1])->begin(),
                        scval_get::symbol((*pv)[1])->end()),
            "two");

  auto m = scval::map({xdr::SCMapEntry{xdr::Box<xdr::SCVal>(scval::symbol("k")),
                                     xdr::Box<xdr::SCVal>(scval::u32(5))}});
  const auto* pm = scval_get::map(m);
  ASSERT_NE(pm, nullptr);
  ASSERT_EQ(pm->size(), 1u);
}

TEST(ScVal, CanonicalOrdering) {
  EXPECT_LT(scval_compare(scval::u32(1), scval::u32(2)), 0);
  EXPECT_GT(scval_compare(scval::u32(3), scval::u32(2)), 0);
  EXPECT_EQ(scval_compare(scval::u32(5), scval::u32(5)), 0);
  // discriminant dominates: U32 < I64 regardless of value
  EXPECT_LT(scval_compare(scval::u32(UINT32_MAX), scval::i64(-1)), 0);
  // vec elementwise
  EXPECT_LT(scval_compare(scval::vec({scval::u32(1)}), scval::vec({scval::u32(2)})), 0);
  EXPECT_LT(scval_compare(scval::vec({scval::u32(1)}), scval::vec({scval::u32(1), scval::u32(0)})), 0);
  // map keys sorted canonically for determinism checks
  EXPECT_LT(scval_compare(scval::symbol("a"), scval::symbol("b")), 0);
}

TEST(Contract, IdDerivation) {
  auto deployer = Address::parse("GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");
  ASSERT_TRUE(deployer.ok());
  std::array<std::uint8_t, 32> salt{};
  salt[0] = 1;
  auto id = contract::id_from_address(Network::testnet(), *deployer, salt);
  ASSERT_TRUE(id.ok());
  // deterministic + network-bound
  EXPECT_EQ(*id, *contract::id_from_address(Network::testnet(), *deployer, salt));
  EXPECT_NE(*id, *contract::id_from_address(Network::public_(), *deployer, salt));
  // different salt => different id
  salt[0] = 2;
  EXPECT_NE(*id, *contract::id_from_address(Network::testnet(), *deployer, salt));
  // valid C strkey
  auto sk = StrKey::decode(contract::contract_strkey(*id));
  ASSERT_TRUE(sk.ok());
  EXPECT_EQ(sk->kind(), StrKeyKind::kContract);
}

TEST(Contract, HostFunctionBuilders) {
  auto fn = contract::invoke_fn(
      "CA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUWDA", "transfer",
      {scval::u32(1), scval::symbol("x")});
  ASSERT_TRUE(fn.ok());
  EXPECT_EQ(fn->type(), xdr::HostFunctionType::HOST_FUNCTION_TYPE_INVOKE_CONTRACT);
  EXPECT_EQ(fn->invokeContract().args.size(), 2u);

  auto deployer = Address::parse("GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ");
  ASSERT_TRUE(deployer.ok());
  std::array<std::uint8_t, 32> salt{};
  auto cf = contract::create_contract_v2_fn(*deployer, salt, contract::sac_executable());
  ASSERT_TRUE(cf.ok());
  EXPECT_EQ(cf->type(), xdr::HostFunctionType::HOST_FUNCTION_TYPE_CREATE_CONTRACT_V2);

  Bytes wasm{1, 2, 3};
  auto up = contract::upload_wasm_fn(wasm);
  EXPECT_EQ(up.type(), xdr::HostFunctionType::HOST_FUNCTION_TYPE_UPLOAD_CONTRACT_WASM);

  // invoke_fn rejects non-contract strkeys
  EXPECT_FALSE(contract::invoke_fn("GA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJVSGZ", "f", {})
                   .ok());
}

TEST(Contract, InvokeHostFunctionOpRoundTrip) {
  auto fn = contract::invoke_fn(
      "CA7QYNF7SOWQ3GLR2BGMZEHXAVIRZA4KVWLTJJFC7MGXUA74P7UJUWDA", "f", {scval::u32(1)});
  ASSERT_TRUE(fn.ok());
  auto op = ops::invoke_host_function(*fn);
  EXPECT_EQ(op.body.type(), xdr::OperationType::INVOKE_HOST_FUNCTION);
  auto bytes = xdr::encode(op);
  ASSERT_TRUE(bytes.ok());
  auto back = xdr::decode<xdr::Operation>(*bytes);
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(*back, op);
}

TEST(Contract, SignAuthEntrySourceAccountPassthrough) {
  xdr::SorobanAuthorizationEntry entry;
  entry.credentials = xdr::SorobanCredentials::sorobanCredentialsSourceAccount();
  entry.rootInvocation = xdr::SorobanAuthorizedInvocation{};
  auto kp = KeyPair::random();
  ASSERT_TRUE(kp.ok());
  auto signed_entry = contract::sign_auth_entry(entry, Network::testnet(), *kp);
  ASSERT_TRUE(signed_entry.ok());
  EXPECT_EQ(signed_entry->credentials.type(),
            xdr::SorobanCredentialsType::SOROBAN_CREDENTIALS_SOURCE_ACCOUNT);
}

// ---------------------------------------------------------------------------
// WASM metadata + spec-driven args + delegate trees
// ---------------------------------------------------------------------------

namespace {

// Build a valid WASM module containing only custom sections (valid per the
// spec — custom sections can appear anywhere and don't require code).
Bytes wasm_with_sections(std::vector<std::pair<std::string, Bytes>> sections) {
  Bytes w = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};  // \0asm + v1
  auto leb = [](std::uint64_t v, Bytes& out) {
    do {
      auto b = static_cast<std::uint8_t>(v & 0x7f);
      v >>= 7;
      if (v) b |= 0x80;
      out.push_back(b);
    } while (v);
  };
  for (const auto& [name, payload] : sections) {
    Bytes s;
    leb(name.size(), s);
    s.insert(s.end(), name.begin(), name.end());
    s.insert(s.end(), payload.begin(), payload.end());
    w.push_back(0x00);  // custom section id
    leb(s.size(), w);
    w.insert(w.end(), s.begin(), s.end());
  }
  return w;
}

}  // namespace

TEST(ContractMeta, CustomSections) {
  // contractmetav0: count=1, "name" -> "my-token" (LEB len-prefixed pairs)
  const Bytes meta = {0x01, 0x04, 'n','a','m','e', 0x08, 'm','y','-','t','o','k','e','n'};
  // contractspecv0: opaque XDR bytes we pass through
  const Bytes spec = {0xde, 0xad, 0xbe, 0xef};
  // contractenvmetav0: count=1, "v1.0.0"
  const Bytes env = {0x01, 0x06, 'v','1','.','0','.','0'};

  const auto wasm = wasm_with_sections({{"contractmetav0", meta},
                                        {"contractspecv0", spec},
                                        {"contractenvmetav0", env}});
  auto m = parse_contract_meta(wasm);
  ASSERT_TRUE(m.ok()) << m.error().describe();
  EXPECT_EQ(m->name, "my-token");
  EXPECT_EQ(m->spec_entries_xdr, spec);
  ASSERT_EQ(m->env_meta.size(), 1u);
  EXPECT_EQ(m->env_meta[0], "v1.0.0");
}

TEST(ContractMeta, SpecDrivenArgs) {
  // primitives
  auto b = scval_from_string("bool", "true");
  ASSERT_TRUE(b.ok() && b->type() == xdr::SCValType::SCV_BOOL);
  EXPECT_TRUE(b->b());
  auto u = scval_from_string("u32", "42");
  ASSERT_TRUE(u.ok() && u->type() == xdr::SCValType::SCV_U32);
  EXPECT_EQ(u->u32(), 42u);
  auto i = scval_from_string("i128", "-5");
  ASSERT_TRUE(i.ok() && i->type() == xdr::SCValType::SCV_I128);
  auto sym = scval_from_string("symbol", "transfer");
  ASSERT_TRUE(sym.ok() && sym->type() == xdr::SCValType::SCV_SYMBOL);
  auto addr = scval_from_string(
      "address", "GAGXKUDVJYEABJOSG7XPLATAGV3GXGZ6LIKYNCUUBKZITFMHRDR3BMEA");
  ASSERT_TRUE(addr.ok() && addr->type() == xdr::SCValType::SCV_ADDRESS);
  // complex types via JSON
  auto vec = scval_from_string("vec", "[1, 2, 3]");
  ASSERT_TRUE(vec.ok() && vec->type() == xdr::SCValType::SCV_VEC);
  auto obj = scval_from_string("map", R"({"a": 1, "b": "x"})");
  ASSERT_TRUE(obj.ok() && obj->type() == xdr::SCValType::SCV_MAP);
  // invalid
  EXPECT_FALSE(scval_from_string("tuple", "not-json").ok());
  EXPECT_FALSE(scval_from_string("address", "not-a-strkey").ok());
}

TEST(DelegateTree, Validation) {
  const Address* pub = nullptr;  // any valid SCAddress
  (void)pub;
  xdr::SCAddress addr = xdr::SCAddress::scAddressTypeAccount(
      xdr::AccountID(xdr::PublicKey::publicKeyTypeEd25519(xdr::uint256{})));
  // cycle: a node that is its own child is rejected
  {
    DelegateNode root{addr, xdr::SCVal::scvVoid(), {}};
    DelegateNode same{addr, xdr::SCVal::scvVoid(), {}};
    root.children.push_back(same);
    EXPECT_FALSE(build_delegate_tree({root}).ok());
  }
  // duplicate siblings are rejected
  {
    DelegateNode a{addr, xdr::SCVal::scvVoid(), {}};
    DelegateNode b{addr, xdr::SCVal::scvVoid(), {}};
    EXPECT_FALSE(build_delegate_tree({a, b}).ok());
  }
  // distinct trees build
  {
    xdr::SCAddress addr2 = xdr::SCAddress::scAddressTypeAccount(
        xdr::AccountID(xdr::PublicKey::publicKeyTypeEd25519(xdr::uint256{1})));
    DelegateNode root{addr, xdr::SCVal::scvVoid(), {}};
    DelegateNode child{addr2, xdr::SCVal::scvVoid(), {}};
    root.children.push_back(child);
    auto t = build_delegate_tree({root});
    ASSERT_TRUE(t.ok());
    ASSERT_EQ(t->size(), 1u);
    ASSERT_EQ((*t)[0].nestedDelegates->size(), 1u);
  }
}

TEST(ContractMeta, RealWasmSpec) {
  // vendored hello_world.wasm (built by `stellar contract init/build`) —
  // real spec stream -> ContractSpec -> args_from_json end to end
  std::ifstream f(std::string(STELLAR_FIXTURES_DIR) + "/../wasm/hello_world.wasm",
                  std::ios::binary);
  ASSERT_TRUE(f.good());
  const Bytes wasm{std::istreambuf_iterator<char>(f), {}};

  auto meta = parse_contract_meta(wasm);
  ASSERT_TRUE(meta.ok()) << meta.error().describe();
  ASSERT_FALSE(meta->spec_entries_xdr.empty()) << "no contractspecv0 section";

  auto spec = ContractSpec::decode(ByteSpan(meta->spec_entries_xdr.data(),
                                            meta->spec_entries_xdr.size()));
  ASSERT_TRUE(spec.ok()) << spec.error().describe();

  // hello(to: String) -> Vec<String>
  const auto* fn = spec->function("hello");
  ASSERT_NE(fn, nullptr);
  ASSERT_EQ(fn->inputs.size(), 1u);
  EXPECT_EQ(std::string(fn->inputs[0].name), "to");
  EXPECT_EQ(fn->inputs[0].type.type(), xdr::SCSpecType::SC_SPEC_TYPE_STRING);

  // spec-driven args: named JSON -> SCVals
  auto args = spec->args_from_json("hello", Json::parse(R"({"to": "world"})").value());
  ASSERT_TRUE(args.ok()) << args.error().describe();
  ASSERT_EQ(args->size(), 1u);
  EXPECT_EQ(args->at(0).type(), xdr::SCValType::SCV_STRING);
  EXPECT_EQ(args->at(0).str(), "world");
}
