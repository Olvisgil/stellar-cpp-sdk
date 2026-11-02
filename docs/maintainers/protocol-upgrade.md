# Protocol upgrade playbook

When a new `rs-stellar-xdr` release lands (the `xdr-drift` workflow opens an issue):

1. Re-vendor the XDR inputs:
   `python3 tools/update_xdr.py --tag <new-tag> --protocol <new-protocol>`
   This fetches `xdr.json` plus the `.x` files at the release's pinned
   `stellar-xdr` commit and verifies their sha256s.
2. Regenerate: `python3 tools/xdrgen/generate.py` and review the diff —
   new unions/arms/structs must get coverage in tests.
3. `cmake --preset dev && cmake --build --preset dev && ctest --preset dev`.
4. Fix SDK layers affected by type changes; add integration tests against the
   new protocol's quickstart image (`stellar/quickstart:testing`) before the
   testnet vote.
5. Bump `protocol=` in `xdr/VERSION`, update `CHANGELOG.md`.
