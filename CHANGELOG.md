# Changelog

## Unreleased — 0.1.0 development

- Protocol 30 XDR types + strict codec generated from `stellar-xdr` `4f524bba`
  (via `rs-stellar-xdr` v30.0.0 `xdr.json`).
- `stellar::Result<T>`/`Error` plumbing, strict hex/base64/base32/CRC16 helpers.
- `ports/stellar-cpp-sdk` vcpkg overlay port + `vcpkg-port` CI job that installs
  the port and builds a downstream consumer against it.
- Install/export fixes for downstream consumers: re-creatable `stellar_sodium`
  + `CURL::libcurl` targets in `StellarSdkConfig.cmake`, `EXPORT_NAME` aligns
  installed targets with the `stellar::{base,xdr,core}` aliases, package
  version tracks `PROJECT_VERSION`, component checks implemented.
- Root `vcpkg.json` trimmed to used deps (libsodium, curl, gtest).
