# Changelog

## 0.2.0

- New optional `stellar::iso20022` component (`STELLAR_SDK_ENABLE_ISO20022`,
  pugixml as a private dependency): emit and parse ISO 20022 XML for
  pacs.008.001.14, pacs.002.001.16, pacs.004.001.15, pain.001.001.13,
  pain.002.001.15, camt.053.001.14 and camt.054.001.14, with a Stellar
  payment bridge (`Payment`/`Party`/`CreditTransferInstruction`), batch
  totals, UETR generation, `SplmtryData` preservation of non-ISO assets and
  exact 7-dp amounts, status-report flattening + `reconcile_instructions`,
  and `ValidationReport` field validation. Official XSDs vendored under
  `iso20022/xsd/`; emitted documents validate against them
  (`tools/check_iso20022_xsd.sh`, needs `xmllint`).

## 0.1.2

- `export.hpp` defaults `STELLAR_API` to static (empty); Windows consumers no
  longer need `STELLAR_SDK_STATIC_DEFINE` to avoid `dllimport` attributes.
- `StellarSdkConfig.cmake` is idempotent — repeated `find_package(StellarSdk)`
  calls no longer fail on a duplicate `stellar_sodium` target.

## 0.1.1

- `ports/stellar-cpp-sdk` vcpkg overlay port + `vcpkg-port` CI job that installs
  the port and builds a downstream consumer against it.
- Install/export fixes for downstream consumers: re-creatable `stellar_sodium`
  + `CURL::libcurl` targets in `StellarSdkConfig.cmake`, `EXPORT_NAME` aligns
  installed targets with the `stellar::{base,xdr,core}` aliases, package
  version tracks `PROJECT_VERSION`, component checks implemented.
- Libraries propagate `cxx_std_20` to consumers via PUBLIC compile features.
- Fix `KeyPair::random()` — previously produced deterministic keys from an
  uninitialized seed.
- Root `vcpkg.json` trimmed to used deps (libsodium, curl, gtest).
- Fix fuzz targets (`stellar::ByteSpan` qualification, `<algorithm>` include).

## 0.1.0

- Protocol 30 XDR types + strict codec generated from `stellar-xdr` `4f524bba`
  (via `rs-stellar-xdr` v30.0.0 `xdr.json`).
- `stellar::Result<T>`/`Error` plumbing, strict hex/base64/base32/CRC16 helpers.
