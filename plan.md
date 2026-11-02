---
agent: devin-local
session: numerous-garage
created: 2026-11-01T18:00:23Z
---
# Stellar C++ SDK — Architecture & Implementation Plan

Build a modern, Apache-2.0, C++20 SDK for the Stellar network (Protocol 30 "Bell") covering XDR, keys, transactions, Stellar RPC, Soroban contracts, Horizon, wallet/anchor SEPs and XDR-JSON, using a `Result<T>` error model and a synchronous, thread-safe API over a pluggable HTTP transport, with heavy emphasis on verification (known-answer vectors, fuzzing, differential and local-network integration tests).

## Context & key findings

- **Repo is greenfield**: only `LICENSE` (Apache-2.0) and an initial commit; remote `github.com/Olvisgil/stellar-cpp-sdk`.
- **Live network state (checked today)**: mainnet and testnet run **protocol 29**; Core/Horizon/RPC **v30** are already deployed. **Protocol 30 "Bell"**: testnet vote **Nov 3 2026**, mainnet vote **Nov 23 2026**. P30 changes the ledger header (CAP-88, millisecond close times), so older decoders break. It also adds muxed contract addresses and `W…` strkeys (CAP-84). CAP-87 (ML-DSA) only affects contracts.
- **Soroban auth changed in P27 (CAP-71)**: there are now 4 credential types. `SOROBAN_CREDENTIALS_ADDRESS_V2` signs the address-bound payload `ENVELOPE_TYPE_SOROBAN_AUTHORIZATION_WITH_ADDRESS` and is the recommended default. `ADDRESS_WITH_DELEGATES` adds delegate signature trees. Legacy `ADDRESS` still works.
- **Horizon is officially nearing end-of-life** (maintenance only). Stellar RPC is the primary API. It has 13 methods, including `queryEvents` (beta). RPC v30 removed `inSuccessfulContractCall` from `getEvents`.
- **XDR tooling licensing**: `xdrpp` (used by stellar-core and by the prior-art `irfanghat/cpp-stellar-sdk`) is **GPLv3**, and only SDF may relicense it. Depending on it would push GPLv3 onto everyone who uses this Apache-2.0 SDK, so it's ruled out. `stellar/xdrgen` is a Ruby library that no longer ships non-Go generators.
- **Ideal codegen input**: SDF's `rs-stellar-xdr` v30.0.0 (Apache-2.0) publishes `xdr-definitions-json/xdr.json`, a resolved JSON AST of all Stellar XDR:
  - 495 definitions: 237 structs, 115 unions, 85 enums, 40 typedefs, 18 consts.
  - Anonymous nested types are already lifted out and named.
  - Each enum lists its `member_prefix`.
  - It records the sha256 of every source `.x` file.
  - It is pinned to stellar-xdr `4f524bba`, the same commit stellar-core 30.0.0 pins.
- **Machine-readable API specs exist**: OpenRPC (`stellar-docs/static/stellar-rpc.openrpc.json`) and Horizon OpenAPI (`stellar-docs/openapi/horizon/main.yml`). These can drive conformance fixtures and auto-generated coverage tables showing which endpoints the SDK supports.
- **Prior art isn't reusable**:
  - `irfanghat/cpp-stellar-sdk` is early-stage and built on xdrpp.
  - `bnogalm/StellarQtSDK` is MPL-2.0, Qt-based, Horizon-only, from 2017, with no Soroban support.
  - Functional references: the JS SDK (SDF; v17.3.0 supports P30), the Go SDK (v0.8.0), and the Soneso SDKs (most complete SEP coverage).

## Decisions

| Area | Decision | Rationale |
|---|---|---|
| Language | **C++20** minimum (GCC 11+, Clang 14+, AppleClang 14+, MSVC 2022) | Your choice; matches stellar-core; gives span, concepts, `<=>` |
| Errors | **`stellar::Result<T>`** (expected/StatusOr-style) with a rich `stellar::Error` (code, message, context chain, HTTP status, RPC error, tx result codes). Works with `-fno-exceptions`; `.value()` throws only when exceptions are enabled | Your choice; same approach as the AWS and Google Cloud C++ SDKs |
| Concurrency | **Synchronous, thread-safe clients** over an `HttpTransport` interface (libcurl by default). Streaming (Horizon SSE) runs on RAII background-thread subscriptions | Your choice; async can be layered on later |
| XDR | Our own **Python (stdlib-only) generator** reads the vendored `xdr.json` and emits modern C++ types plus a strict codec. **Generated code is committed** | Clean licensing, uses SDF's canonical AST, and consumers don't need Python to build |
| 1.0 scope | Core + RPC + contracts + **Horizon** + **wallet/anchor SEPs** + **XDR-JSON** | Your choice; C API deferred |
| Build | CMake ≥ 3.24 with presets; vcpkg manifest as the primary dependency source; `find_package(StellarSdk COMPONENTS …)` exports | Standard practice |
| Dependencies | libsodium (ISC), libcurl (MIT-style), nlohmann/json (MIT, private), toml++ (MIT, SEP module only), GoogleTest/GMock (tests), Google Benchmark (optional) | All Apache-compatible. **No third-party types appear in public headers.** Pin versions published ≥7 days ago |
| Naming | Namespace `stellar` with an inline ABI namespace `v1`. Types are PascalCase, functions snake_case. Generated XDR keeps protocol names (`stellar::xdr::TransactionEnvelope`, field `sourceAccount`) | Matches the protocol docs 1:1 and avoids symbol clashes with stellar-core's `stellar::` types |

## Architecture (CMake targets)

Each layer can be used on its own. For example, an air-gapped signer links only `stellar::core`.

```
stellar::sdk        umbrella (INTERFACE)
├── stellar::sep       network SEPs 1, 2, 6, 7, 9, 10, 12, 24, 29, 31, 38, 45   → contract, rpc, http (+ toml++ private)
├── stellar::horizon   REST + SSE client (option STELLAR_SDK_WITH_HORIZON)       → core, http
├── stellar::contract  ScVal, spec/WASM meta, simulate→assemble, auth, deploy,
│                      events, SEP-41 token client                               → rpc
├── stellar::rpc       typed JSON-RPC client + helpers                           → core, http
├── stellar::http      HttpTransport iface, retry/backoff, multipart, SSE parser,
│                      CurlTransport (option STELLAR_SDK_WITH_CURL), MockTransport
├── stellar::xdr_json  XDR-JSON (SEP-51)                                         → xdr (+ nlohmann private)
├── stellar::core      encoding utils, SEP-23 strkeys, keys & Signer, networks, hashing,
│                      Amount/Price/Int128/Int256, assets, addresses, memos,
│                      tx builder & signing, SEP-5 mnemonics, SEP-53 messages     → xdr (+ libsodium private)
└── stellar::xdr       generated P30 types + strict binary codec + base64         (stdlib only)
```

Cross-cutting interfaces (injectable, so tests are deterministic):
- `Clock` and `RandomSource`: time bounds, nonces.
- `Logger`: redacts secrets, JWTs and `Authorization` headers.
- `Signer`: HSM, KMS, Ledger, Secure Enclave or remote signers.
- `HttpTransport`: Qt, Asio, Unreal or mock implementations.

## Public API sketch (indicative; finalized in the API review)

```cpp
#include <stellar/sdk.hpp>
using namespace stellar;

STELLAR_TRY_ASSIGN(kp, KeyPair::from_secret(secret_seed));          // Result<KeyPair>
rpc::Client rpc{"https://soroban-testnet.stellar.org"};
STELLAR_TRY_ASSIGN(account, rpc.get_account(kp.account_id()));

STELLAR_TRY_ASSIGN(tx, TransactionBuilder{account, Network::testnet()}
    .base_fee(100)
    .timeout(std::chrono::seconds{30})                               // required unless no_timeout()
    .add(op::payment({.destination = dest, .asset = Asset::native(),
                      .amount = Amount::parse("10.5").value()}))
    .build());
tx.sign(kp);
STELLAR_TRY_ASSIGN(applied, rpc.send_and_wait(tx));                  // polls getTransaction w/ backoff

contract::Client token{rpc, ContractId::parse("CB...").value(), Network::testnet()};
auto r = token.invoke("transfer", {Address{from}, Address{to}, scval::i128(1'000)},
                      {.source = kp.account_id(), .signer = &kp});  // simulate→assemble→auth→sign→send→wait
```

## Repository layout

```
CMakeLists.txt  CMakePresets.json  vcpkg.json  .clang-format  .clang-tidy  .editorconfig
AGENTS.md  README.md  CHANGELOG.md  CONTRIBUTING.md  SECURITY.md  NOTICE  LICENSE (existing)
cmake/            StellarSdkConfig.cmake.in, Warnings.cmake, Sanitizers.cmake, Install.cmake
xdr/              VERSION (stellar-xdr 4f524bba / rs-stellar-xdr v30.0.0), xdr.json, *.x (sha256-verified)
tools/xdrgen/     generate.py + AST loader + C++ emitters + generator unit tests (stdlib-only Python)
tools/update_xdr.py   fetch a release and verify .x sha256s against xdr.json
tools/compat/     builds the RPC/Horizon/SEP coverage tables from the OpenRPC/OpenAPI specs
include/stellar/  sdk.hpp result.hpp error.hpp version.hpp export.hpp
                  xdr/{runtime,codec,bounded,box}.hpp xdr/generated/*.hpp  xdr_json/
                  core/ tx/ http/ rpc/ contract/ horizon/ sep/
src/              mirrors include/; private deps (sodium, curl, json, toml) only used here
                  src/xdr/generated/*.cpp
tests/            unit/ vectors/ conformance/ integration/ differential/ fuzz/ fixtures/wasm/
examples/  benchmarks/  docs/ (guides, maintainers/protocol-upgrade.md, Doxyfile)  compatibility/
.github/workflows/  ci.yml sanitizers.yml integration.yml fuzz.yml nightly.yml docs.yml release.yml xdr-drift.yml
```

## Implementation phases

Every phase must finish with CI green on all platforms and ships with its own tests. A 0.1.0 pre-release follows Phase 5.

### Phase 0 — Foundations
1. **CMake skeleton**: top-level and per-component `CMakeLists.txt`, plus `CMakePresets.json` presets: `dev`, `release`, `asan-ubsan`, `tsan`, `coverage`, `fuzz`, `msvc`.
2. **`cmake/` helpers**: warnings (`-Werror` in CI), sanitizers, hidden visibility plus `STELLAR_API`, install/export with components.
3. **`vcpkg.json`** with a pinned baseline.
4. **`Result<T>`/`Error`/`ErrorCode`**:
   - `STELLAR_TRY` / `STELLAR_TRY_ASSIGN` macros.
   - `std::expected`-like monadic API: `and_then`, `transform`, `or_else`.
5. **Utilities**: strict hex, base64 and base32 (RFC 4648), CRC16-XModem, constant-time compare, and `SecretBytes` (zeroed by libsodium on destruction). Also the `Clock`, `RandomSource` and `Logger` interfaces, and `version.hpp` (SDK version, supported protocol, XDR commit).
6. **CI** (GitHub Actions): Ubuntu (GCC 11/14, Clang 14/19), macOS (AppleClang arm64), Windows (MSVC 2022, static and shared). Plus clang-format/clang-tidy checks, CodeQL and dependency review.
7. **Docs scaffolding**: README, CONTRIBUTING, SECURITY.md (disclosure policy), CHANGELOG, and AGENTS.md with build/test commands.

### Phase 1 — XDR layer (`stellar::xdr`)
1. **Vendor inputs**: `xdr/xdr.json` and the `.x` files at stellar-xdr `4f524bba`. `tools/update_xdr.py` verifies their sha256s.
2. **`tools/xdrgen`** (Python ≥ 3.10, stdlib only, with its own tests):
   - Load the AST, resolve typedefs and order types by dependency.
   - Detect recursive type cycles. Insert `xdr::Box<T>` only where the recursion isn't already broken by a vector.
   - Emit types in dependency order with forward declarations, one header/source pair per `.x` file.
3. **Type mapping**:

   | XDR | C++ |
   |---|---|
   | `int`, `unsigned int`, `hyper`, `unsigned hyper`, `bool` | fixed-width integer types, `bool` |
   | `enum` | `enum class : int32_t` plus name↔value tables |
   | `struct` | aggregate with defaulted `==` |
   | `union` | generated tagged class over `std::variant`, with factories, `type()`, checked accessors, `get_if_*()`, `visit()` |
   | `T<N>` | `BoundedVector<T,N>` |
   | `opaque<N>` | `BoundedBytes<N>` |
   | `opaque[N]` | `std::array<uint8_t,N>` |
   | `string<N>` | `BoundedString<N>` (raw bytes, not assumed UTF-8) |
   | `T*` | `std::optional<T>` or `OptionalBox<T>` |
   | `const` | `inline constexpr` |

   Union alternatives are selected by index, not by type, because several arms share a payload type (e.g. `ADDRESS` and `ADDRESS_V2`).
4. **Codec**:
   - Big-endian with 4-byte alignment.
   - **Strict decoding**: zero padding, bool ∈ {0,1}, known enum values and union discriminants, length ≤ max, optional flag ∈ {0,1}, no trailing bytes.
   - Configurable depth and length limits, so crafted input can't exhaust the stack or memory.
   - Errors include the field path (e.g. `TransactionEnvelope.v1.tx.operations[2].body.payment.amount`).
   - API: `xdr::encode`, `xdr::decode<T>`, `to_base64`/`from_base64`, `xdr_size`.
5. **Generated reflection**: `visit_fields`, type names, and an `arbitrary<T>()` random-value generator. A runtime type registry allows decode-by-name, which powers a small `stellar-xdr-cpp` debug CLI.
6. **Tests**:
   - Round-trip property tests for all 495 types.
   - Known-answer fixtures captured from mainnet/testnet: envelopes, results, transaction metas, and ledger close metas including P30 headers.
   - Malformed-input rejection suites.
   - libFuzzer targets for each top-level type.
   - A CI check that fails if regenerating the code produces a diff.

### Phase 2 — Core primitives (`stellar::core`)
1. **`StrKey`**: all 10 kinds (G, M, S, T, X, P, C, L, B, W), applying SEP-23 v1.4.0's strict rules (length mod 8, zero unused bits, exact re-encoding). Passes every valid and invalid SEP-23 test vector.
2. **Keys and signing**:
   - `KeyPair` uses libsodium Ed25519, so signature verification behaves exactly as in stellar-core.
   - `SecretSeed` is zeroed on destruction and can never be printed.
   - Abstract `Signer` interface.
   - Decorated-signature hints, signed-payload hints (CAP-40), and hash-x signatures.
3. **`Network`**: passphrase → network ID. Constants for public, testnet, futurenet and standalone. A network is always passed explicitly; there is no default.
4. **Numerics**:
   - `Amount`: int64 stroops with exact 7-decimal parse/format and overflow checks; never floating point.
   - `Price`: exact rational, using the best-rational-approximation algorithm checked against the JS SDK's test vectors.
   - Portable `Int128/UInt128/Int256/UInt256` with decimal conversion, comparison and basic arithmetic. No `__int128`, so it works on MSVC.
5. **Domain types**:
   - `Asset`: validation, canonical `CODE:ISSUER` form, `ChangeTrustAsset`/`TrustLineAsset`, liquidity-pool ID calculation, Stellar Asset Contract (SAC) contract-ID derivation.
   - `Address`, `MuxedAccount`, `ContractId`, `MuxedContract`, convertible to and from the XDR `SCAddress`/`MuxedAccount`.
   - `Memo`.
6. **Offline SEPs**:
   - SEP-5: BIP-39 mnemonics with the official wordlists, PBKDF2-HMAC-SHA512 built on libsodium, SLIP-10 derivation `m/44'/148'/n'`, checked against the SEP-5 test vectors.
   - SEP-53: sign and verify messages with the `"Stellar Signed Message:\n"` prefix, checked against the spec vectors.

### Phase 3 — Transactions (`stellar::core`, `tx/`)
1. **`TransactionBuilder`**:
   - Source account (ID + sequence), base fee and memo.
   - Preconditions: time bounds are mandatory unless `no_timeout()` is called explicitly. Also ledger bounds, min sequence number/age/gap, and up to 2 extra signers.
   - Up to 100 operations and Soroban data.
   - Validates everything and returns `Result<Transaction>`.
2. **Operation factories** for all 27 operation types, each validating its fields. `allow_trust` and `inflation` are marked deprecated.
3. **`Transaction` / `FeeBumpTransaction`**:
   - `hash(network)` computed via `TransactionSignaturePayload`.
   - `sign(Signer)`, attaching external signatures for multisig, and verifying signatures.
   - Parse and serialize v0, v1 and fee-bump envelopes (base64).
   - Fee helpers for classic transactions and for Soroban (resource fee + inclusion fee).
4. **Cross-SDK test fixtures**: a script generates envelopes and hashes with JS SDK v17.3.0 and Go `txnbuild` v0.8.0 for a matrix of operations and preconditions. Our output must match byte for byte.

### Phase 4 — HTTP & RPC (`stellar::http`, `stellar::rpc`)
1. **`HttpTransport` interface**: `send`, plus `stream` with cancellation.
2. **`CurlTransport`**: TLS verification on, connection reuse, HTTP/2 when available, proxy and CA-bundle settings, response-size cap, timeouts.
3. **Middleware**:
   - Retry with exponential backoff and jitter, honoring `Retry-After`/429; applies to read calls by default.
   - `X-Client-Name/Version` headers.
   - Logging and metrics hooks.
4. **Supporting pieces**: multipart encoder, SSE parser, and `MockTransport`.
5. **`rpc::Client`**:
   - All 13 methods, including `queryEvents` (beta; used only if the server supports it).
   - `simulateTransaction` options: `resourceConfig.instructionLeeway`, `authMode`, `useUpgradedAuth`.
   - Optional `xdrFormat=json` passthrough.
6. **Lazy XDR decoding**: typed responses keep the raw base64 and decode on demand into `Result<xdr::T>`. After a protocol upgrade, one unfamiliar structure doesn't break the whole response.
7. **Helpers**:
   - `get_account`, and ledger-entry batching (≤ 200 keys per request, chunked automatically).
   - Contract data and code lookup.
   - `send_and_wait`: handles PENDING, DUPLICATE, TRY_AGAIN_LATER and ERROR, and polls with backoff until a deadline.
   - Fee recommendations from `getFeeStats`, and Friendbot funding.
8. **Error mapping**: JSON-RPC errors, HTTP errors, decoded `TransactionResult` codes, and diagnostic events.
9. **Conformance**: every example in the OpenRPC spec must parse. `tools/compat` generates `compatibility/rpc.md`.

### Phase 5 — Smart contracts (`stellar::contract`) → tag **0.1.0**
1. **ScVal conversion** for all 23 `SCValType`s:
   - `to_scval` / `from_scval<T>` for native types, `Symbol`, `Bytes`, 128/256-bit integers, vectors, `std::optional`, `Address`, timepoint and duration.
   - `std::map` keys are **sorted canonically**, because the host rejects unsorted maps.
2. **Contract metadata**:
   - A WASM custom-section reader (no execution) extracts SEP-46/47/48 metadata and the `ScSpecEntry` stream.
   - Fetch a contract's WASM by contract ID via RPC.
   - Spec-driven conversion of arguments and return values (structs, tuples, enums, unions, error enums).
3. **`contract::Client` + `AssembledTransaction`**:
   - Pipeline: build `InvokeHostFunction` → simulate → detect read-only calls → assemble (Soroban data, resource fee with configurable leeway, auth) → sign auth entries → sign envelope → send → poll → decode the return value.
   - Optional automatic handling of the "restore archived state first" response, `TRY_AGAIN_LATER` handling, and optional fee bump.
4. **Authorization**:
   - Signature payload builders for all credential types: `ADDRESS` (legacy), **`ADDRESS_V2` (default)**, and `ADDRESS_WITH_DELEGATES`.
   - A delegate-tree builder that enforces CAP-71-01's ordering and no-duplicate rules.
   - The signature ScVal format used by classic accounts.
   - Signature payloads are exposed directly so smart-account and custom signers can sign them.
   - An invocation-tree view, so wallets can show users what they are authorizing.
5. **Deploy and lifecycle**: upload WASM, create contracts (V2, with constructor args), deploy a SAC, derive contract IDs, extend TTL, restore footprint.
6. **Events**: typed decoding, filter builders, and SEP-41 token-event helpers, including muxed `W` destinations. A `TokenClient` for SEP-41 tokens and SACs.
7. **Integration tests** on a local quickstart network, using vendored, checksummed sample WASMs: hello world, increment, auth, token, and a delegate-auth custom account.

### Phase 6 — Horizon (`stellar::horizon`, optional)
1. **All endpoint families**:
   - Root, ledgers, and fee stats.
   - Accounts (and sub-resources), assets, claimable balances, liquidity pools, offers, order book.
   - Operations, effects, payments, trades, trade aggregations.
   - Strict-send/receive paths.
   - Transactions: get, submit, and `transactions_async`.
2. **Requests and paging**: request builders (cursor/limit/order, filters) and `Page<T>` with `next()`/`prev()` following `_links`.
3. **SSE streaming**: reconnects and resumes from the last cursor; RAII `Subscription`.
4. **Typed models** for all 27 operation types and all effect types. Unknown types fall back to `Unknown{type, raw_json}`, so newer Horizon versions don't break parsing.
5. **Errors**: problem+json responses, including `extras.result_codes` and a decoded `result_xdr`.
6. **Tests**: conformance against the OpenAPI spec's examples, a generated `compatibility/horizon.md`, and quickstart integration tests (payments, path payments, offers, claimable balances, sponsorship, liquidity pools, streaming).

### Phase 7 — Wallet/anchor SEPs (`stellar::sep`)
1. **Discovery and addressing**:
   - SEP-1: HTTPS-only fetch with the spec's 100 KB cap, typed `StellarToml` model.
   - SEP-2: forward and reverse federation lookups.
   - SEP-7: parse and build `tx`/`pay` URIs; sign and verify them using `URI_REQUEST_SIGNING_KEY`.
   - SEP-9: KYC field models.
2. **SEP-10 web auth**:
   - Fetch the challenge and validate it against the spec's full checklist: server key, home and web-auth domains, time bounds, nonce, sequence 0, operation sources.
   - Sign with one or more signers (multisig) and return the JWT.
   - Client-domain signing via a callback.
3. **SEP-45 web auth for contract accounts**:
   - Strictly validate the challenge's authorization entries: the `web_auth_verify` call on the server's contract, with no sub-invocations.
   - Sign the client's entry through a `Signer`.
4. **Anchor flows**: SEP-12 (customer GET/PUT/DELETE, verification, callbacks, multipart file upload), SEP-6, SEP-24, SEP-31, SEP-38.
5. **SEP-29**: memo-required check using RPC data entries, available as an opt-in guard before submitting.
6. **Defaults**: HTTPS is enforced (explicit opt-in for localhost development). JWTs are treated as secrets and never logged.
7. **Tests**: mock-transport suites covering every SEP-10/45 rejection rule, plus a nightly run against the `stellar/anchor-platform` image on a local network.

### Phase 8 — XDR-JSON (`stellar::xdr_json`; can run in parallel any time after Phase 1)
1. **Encoder/decoder**: a generic encoder/decoder driven by the generated reflection. It also needs special cases to match `rs-stellar-xdr`'s JSON output exactly (strkeys for IDs, hex for opaque data, escaped-ASCII strings, enum names without their prefix, 128/256-bit integers, and so on).
2. **Differential tests** against the pinned Rust `stellar-xdr` CLI v30.0.0, using real network data and random `arbitrary<T>()` values:
   - The JSON must be identical.
   - XDR round-trips must be byte-identical.
   - Output must validate against the CLI's JSON Schema.
3. **Documentation**: the cases that don't round-trip are listed in the docs.

### Phase 9 — Hardening & 1.0.0
1. Longer fuzzing runs covering every decoder and parser (JSON, TOML, URI) and the SEP-10/45 validators; apply to OSS-Fuzz.
2. API review (naming, `Result` ergonomics, header hygiene).
3. Documentation: Doxygen site on GitHub Pages, guides modeled on the JS SDK's, and examples that compile in CI.
4. Packaging: vcpkg port and Conan recipe, `find_package` install tests, SBOM, signed release tags with provenance.
5. An independent review of security-critical code: signing, auth payloads, SEP-10/45 validation.
6. Protocol-upgrade playbook (`docs/maintainers/protocol-upgrade.md`), plus a weekly `xdr-drift` workflow that opens an issue when a newer stellar-xdr/rs-stellar-xdr release appears.

## Verification strategy
- **Every PR**:
  - Build and unit tests across the full compiler matrix.
  - ASan+UBSan and TSan jobs.
  - clang-tidy and format checks.
  - Codegen-drift check.
  - Quick fuzz run (~60 s per target).
  - Examples build.
- **Integration (Linux CI)**: the `stellar/quickstart` GitHub Action in `--local` mode (RPC + Horizon + Friendbot, passphrase `Standalone Network ; February 2017`).
- **Nightly**:
  - Long fuzz runs.
  - Testnet smoke tests.
  - The anchor-platform SEP suite.
  - `quickstart:testing`/`futurenet` runs for upcoming protocols.
  - A refresh of the XDR-JSON differential corpus.
- **Coverage**: ≥ 90 % line coverage for `xdr`, `core` and `contract`, reported via Codecov.
- **Local commands** (recorded in AGENTS.md):
  - `cmake --preset dev && cmake --build --preset dev && ctest --preset dev`
  - `python3 tools/xdrgen/generate.py --check`

## Security
- **Secrets**: buffers are zeroed on destruction, can't be implicitly converted to strings or streamed, and must be revealed explicitly. The logger redacts secrets.
- **Networks**: a network passphrase must be passed explicitly for every hash and signature; there are no global defaults.
- **Untrusted input**: parsing is strict and bounded everywhere (XDR depth/length limits, size caps for JSON/TOML, SEP-1's 100 KB).
- **Transport**: TLS verification is always on in the default transport; SEP calls are HTTPS-only by default.
- **Crypto**: all cryptography comes from libsodium (no custom primitives); secrets are compared in constant time.
- **Supply chain**: pinned dependencies (≥ 7 days old), sha256-verified vendored XDR, SBOM, CodeQL, and a SECURITY.md disclosure policy.

## Risks & mitigations
- **Protocol upgrade pace** (P27 through P30 in about a year): target P30 XDR from day one, detect upstream XDR changes automatically, decode XDR lazily in the clients, and follow the playbook to ship before each testnet vote. Upcoming protocols are tracked on a `protocol-next` branch.
- **Dependency on the `xdr.json` format**: the AST loader is isolated, so a `.x` parser can replace it if the format changes or the file is discontinued.
- **XDR-JSON fidelity**: matching the Rust library's JSON details is subtle, and the schema isn't stable across protocols. The differential tests are pinned per release.
- **Compile time and binary size** (~495 `std::variant`-heavy generated types): put codec code in `.cpp` files, one translation unit per `.x` file, measure in CI, and offer an optional unity build.
- **C++ ABI**: standard-library types in the API mean prebuilt binaries must match the user's toolchain. Mitigations: recommend source builds via vcpkg/Conan, use the inline versioned namespace, and defer the C API.
- **Horizon end-of-life**: ship it as an optional module limited to the current OpenAPI spec, and steer new apps to RPC in the docs.
- **Scope size**: each phase delivers something usable, and 0.1.0 after Phase 5 gets early feedback.
- **BIP-39 Unicode passphrases** require NFKD normalization: accept ASCII by default and document the requirement, with optional `utf8proc` support.

## Post-1.0 backlog
- C API/FFI.
- An async layer (futures or a coroutine adapter over the transport).
- A generator for typed C++ contract bindings from contract specs.
- SEP-8, SEP-11 (Txrep), SEP-30.
- Channel-account / sequence-pool helpers.
- Ledger hardware-wallet `Signer`.
- Emscripten/WASM fetch transport.
- Smart-account / passkey (secp256r1/WebAuthn) helpers.

## Immediate next step after approval
Start Phases 0 and 1: scaffold the build and CI, then land the XDR generator with the vendored P30 `xdr.json`, generated types, strict codec, round-trip/property tests and fuzz targets.