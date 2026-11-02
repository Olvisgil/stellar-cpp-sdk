# Contributing

## Setup
    cmake --preset dev && cmake --build --preset dev && ctest --preset dev

## Rules
- Never edit `include/stellar/xdr/generated/types.hpp` or `src/xdr/generated/*`
  by hand — change `tools/xdrgen/generate.py` and regenerate. CI verifies
  byte-identical regeneration (`generate.py --check`) and pinned-input hashes
  (`update_xdr.py --check`).
- Errors flow as `stellar::Result<T>`; public APIs don't throw.
- No third-party types in public headers (sodium/curl/json/toml are private).
- clang-format the handwritten sources you touch (`.clang-format`; generated
  files are exempt).
- Tests required for new functionality; CI runs asan/ubsan/tsan + a fuzz pass.
