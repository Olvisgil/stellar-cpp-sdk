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

## Verifying a change — checklist before calling it done

Local (run what the change touches):
- `cmake --preset dev && cmake --build --preset dev && ctest --preset dev`
- `cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan`
- New XML messages: emit a document and run `tools/check_iso20022_xsd.sh file.xml`
  (xmllint against the vendored schemas — unit tests alone missed real
  choice/wrapper violations; do not skip this)

New dependency — must be added in FIVE places or CI breaks:
1. `CMakeLists.txt` find logic + `cmake/StellarSdkConfig.cmake.in` `find_dependency`
2. root `vcpkg.json` (Windows/msvc CI consumes the manifest)
3. `ports/stellar-cpp-sdk/vcpkg.json` (+ `port-version` bump, then `x-add-version`)
4. `.github/workflows/*.yml` dep installs — `lib<name>-dev` on every ubuntu
   `apt-get install` line, `<name>` on every `brew install` line
   (ci.yml, sanitizers.yml, codeql.yml, fuzz.yml — all of them)
5. `.devcontainer`/`docs` setup notes if they list prerequisites

After pushing, verify the checks — do not assume green:
    gh run list --limit 10                 # ci / sanitizers / codeql / fuzz / xdr-drift / vcpkg-port
    gh run view <id> --log-failed | less   # failures; grep for "error"
    gh run watch <id>                      # block until a run finishes
A workflow that fails at `cmake --preset …` configure with
"Could not find <PKG>" means step 4 above was missed.

Release flow: tag + `git push origin main vX.Y.Z` → `gh release create` →
update port `version`/tarball `SHA512` → "Port: fetch vX.Y.Z release tarball" →
`x-add-version` → "Registry: add X.Y.Z" → push. The port commit cannot be
made before the tag exists (the SHA512 is of the GitHub release tarball).

## Conventions
- C++20, `stellar::inline v1` namespace; errors returned as `stellar::Result<T>` (no exceptions).
- Public headers must not expose third-party types (libsodium/curl/json are private).
- Generated XDR keeps protocol names verbatim (PascalCase types, camelCase fields).
- Never edit generated files directly; change the generator and regenerate.
