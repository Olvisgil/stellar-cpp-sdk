# stellar-cpp-sdk

A modern, **Apache-2.0**, **C++20** SDK for the **Stellar network**
(Protocol 30 "Bell") — XDR, keys, transactions, **Stellar RPC**, **Soroban
smart contracts**, Horizon, and wallet/anchor SEPs.

> Built from scratch on SDF's canonical `xdr.json` AST (no GPLv3 `xdrpp`),
> a `Result<T>` error model, and a synchronous, thread-safe API over a
> pluggable `HttpTransport`. Verified end-to-end on live testnet.

## Highlights

- **Full XDR layer** — 495 definitions generated from stellar-xdr `4f524bba` /
  rs-stellar-xdr v30.0.0, strict big-endian codec (zero padding, depth/length
  limits, field-path errors), runtime type registry, `stellar-xdr-cpp` CLI.
- **Core primitives** — all 10 SEP-23 strkey kinds (incl. CAP-84 `W` muxed
  contracts), libsodium Ed25519 `KeyPair`/`Signer`, `Amount`/`Price`/`Int128`/
  `UInt128`/`Int256`/`UInt256` (with arithmetic + decimal), `Address`/`Asset`/
  `Memo`, SEP-5 mnemonics, SEP-53 message signing.
- **Transactions** — `TransactionBuilder` (mandatory time bounds unless
  `no_timeout()`, 100-op cap, `PRECOND_V2`), all 27 op factories, fee-bump,
  V0→V1 signing, CAP-40 signed payloads, hash-x signatures, decorated sigs.
  **Byte-for-byte verified against stellar-base** across a 32-case matrix
  (`tools/compat/gen_fixtures.mjs`).
- **Stellar RPC** — all 13 methods incl. `queryEvents`/`getTransactions`,
  strict JSON (own parser — zero JSON dep), typed XDR decode with lazy-fallback,
  `stream`/`CancelToken`, `RetryingTransport` (backoff+jitter, `Retry-After`),
  `X-Client-Name/Version`, simulate options, 200-key batching, friendbot,
  `send_and_poll` with `TRY_AGAIN_LATER` backoff + `DUPLICATE` handling.
- **Soroban contracts** — `ScVal` constructors/extractors + canonical sorting,
  `ContractClient` (call/invoke/deploy with `InvokeOptions`: leeway, fee-bump,
  auto-restore), auth signing all 3 credential arms + CAP-71-01 delegate trees,
  `parse_contract_meta` (WASM custom-section reader) + `ContractSpec`
  (spec-driven JSON args end-to-end), `TokenClient` (SEP-41) + event helpers.
- **Infrastructure** — `Clock`/`RandomSource`/`Logger` injectables, `MockTransport`,
  multipart/SSE parsers, `install()` + `find_package(StellarSdk COMPONENTS …)`.
- **ISO 20022** (`stellar::iso20022`, optional) — emit/parse pacs.008, pacs.002,
  pacs.004, pain.001, pain.002, camt.053/054 XML mapped to Stellar payments;
  batch totals, UETR, `SplmtryData` asset/amount preservation, status-report
  reconciliation, `ValidationReport`. Output validates against the official
  XSDs vendored in `iso20022/xsd/` — see `docs/iso20022-mapping.md`.

## Status

**v0.1.0** — Phases 0–5 of `plan.md` complete. Phases 6–7 (Horizon REST/SSE,
network SEPs, SEP-51 XDR-JSON) are next. The SDK already runs end-to-end on
live testnet: deploys WASM, invokes contracts (spec-driven), signs auth, and
handles every RPC method.

**Verified**: 84/84 tests green + 5 live testnet integration tests (friendbot
funding, account fetch, SAC `symbol()`/`transfer` invoke, vendored-WASM
deploy+invoke). 100% on `dev` and `asan-ubsan` presets.

## Build

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

Requires CMake ≥ 3.24 and a C++20 compiler (GCC 11+, Clang 14+,
AppleClang 14+, MSVC 2022+). `vcpkg.json` pins libsodium + libcurl.

Other presets: `release`, `asan-ubsan`, `tsan`, `fuzz`, `msvc`.

## Install

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix /usr/local
```

Or via vcpkg — either from the in-repo git registry by adding this to your
`vcpkg-configuration.json`:

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/Olvisgil/stellar-cpp-sdk",
      "baseline": "cdd0db69bd28f8be78f4fb654247f1ccab266835",
      "packages": ["stellar-cpp-sdk"]
    }
  ]
}
```

or from a checkout via the overlay port (`STELLAR_SDK_LOCAL_SOURCE` builds a
specific tree instead of the tagged release):

```sh
vcpkg install stellar-cpp-sdk --overlay-ports=/path/to/stellar-cpp-sdk/ports
```

Then in your project:

```cmake
find_package(StellarSdk REQUIRED COMPONENTS core xdr rpc contract)
target_link_libraries(my_app PRIVATE stellar::core stellar::xdr stellar::rpc)
```

## Usage

```cpp
#include <stellar/sdk.hpp>
using namespace stellar;

// keys + network (always explicit — no global default)
STELLAR_TRY_ASSIGN(kp, KeyPair::from_secret(secret_seed));
Network net = Network::testnet();

// transactions
TransactionBuilder tx(kp.account_id());
tx.sequence_number(123).fee(100).time_bounds(0, 3600)
  .add_operation(ops::payment(dest, Asset::native(), *Amount::parse("10")));
STELLAR_TRY_ASSIGN(built, tx.build());
auto env = TransactionEnvelope::of_transaction(*built);
env.sign(*kp, net);

// RPC
CurlTransport http;
RpcClient rpc(http, "https://soroban-testnet.stellar.org");
STELLAR_TRY_ASSIGN(sent, rpc.send_and_poll(env));

// contracts (spec-driven)
ContractClient cc(rpc, net, "C...CONTRACT_ID");
cc.set_spec(*ContractSpec::decode(meta.spec_entries_xdr));
auto r = cc.call_json(kp.account_id(), "transfer",
                      Json::parse(R"({"to":"G...","amount":"100"})").value());
```

## Layout

```
include/stellar/   public headers (core/ tx/ rpc/ contract/ http/ xdr/ util/)
src/               implementations (private deps stay here)
tests/             unit + integration + fuzz + fixtures (incl. vendored wasm)
tools/             xdrgen (generator), update_xdr.py, compat/, stellar_xdr_cli
compatibility/     rpc.md conformance matrix (generated by tools/compat)
plan.md            architecture + phase breakdown
```

## License

Apache-2.0. See `LICENSE` and `NOTICE`.
