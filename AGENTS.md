# Agent notes — stellar-cpp-sdk

## Layout
- `include/stellar/` public headers (`result.hpp`, `error.hpp`, `util/`, `xdr/`, `iso20022/`)
- `iso20022/xsd/` — vendored ISO 20022 XML schemas (IPR policy permits reproduction; see `NOTICE`)
- `tools/check_iso20022_xsd.sh` — validates ISO 20022 XML against the vendored XSDs (needs xmllint)
- `include/stellar/xdr/generated/types.hpp` — GENERATED, do not edit by hand
- `src/xdr/generated/*.cpp` — GENERATED codecs + registry
- `xdr/` — pinned XDR inputs (`VERSION` records the commit/tag pins)
- `ports/` — vcpkg overlay port (`STELLAR_SDK_LOCAL_SOURCE` builds a local checkout)
- `versions/` — vcpkg registry database; regenerate after editing `ports/`
  (vcpkg is bootstrapped in-tree at `.vcpkg-root/`; the explicit
  `--vcpkg-root` is required because the directory name is vcpkg's own
  root marker and would otherwise misdetect this repo as the root):
  `.vcpkg-root/vcpkg x-add-version stellar-cpp-sdk --vcpkg-root=$PWD/.vcpkg-root --x-builtin-ports-root=$PWD/ports --x-builtin-registry-versions-dir=$PWD/versions`
- `tools/xdrgen/generate.py` — C++ generator (stdlib-only Python 3.10+)
- `tools/update_xdr.py` — fetch/verify the pinned XDR inputs

## Build & test (macOS/Linux)
    cmake --preset dev && cmake --build --preset dev && ctest --preset dev
Sanitizers: `cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan`

## Regenerate / verify generated code
    python3 tools/xdrgen/generate.py        # regenerate in place
    python3 tools/xdrgen/generate.py --check  # CI drift check
    python3 tools/update_xdr.py --check       # verify xdr/ pins offline

## Conventions
- C++20, `stellar::inline v1` namespace; errors returned as `stellar::Result<T>` (no exceptions).
- Public headers must not expose third-party types (libsodium/curl/json are private).
- Generated XDR keeps protocol names verbatim (PascalCase types, camelCase fields).
- Never edit generated files directly; change the generator and regenerate.
