# Cross-SDK fixtures

`gen_fixtures.mjs` builds a matrix of transactions with stellar-base
(JS SDK) and emits `fixtures_js.json`: `{name, envelopeXdr, txHash}`.
Ed25519 is deterministic, so signed envelopes must be byte-identical
across SDKs.

Regenerate: `npm install stellar-base@11.0.1 && node gen_fixtures.mjs`,
then copy `fixtures_js.json` into `tests/fixtures/cross_sdk/`.

Verified by `CrossSdk.JSEnvelopesByteIdentical` — rebuilds every case
with the C++ `TransactionBuilder` and compares bytes + hashes.
