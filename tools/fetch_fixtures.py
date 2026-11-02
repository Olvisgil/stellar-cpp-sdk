#!/usr/bin/env python3
# Copyright 2026 The stellar-cpp-sdk Authors
# SPDX-License-Identifier: Apache-2.0
"""Capture known-answer XDR fixtures from the live network.

Fetches real TransactionEnvelope / TransactionResult / TransactionMeta /
LedgerCloseMeta values (base64) via Stellar RPC and writes them to
tests/fixtures/xdr/ plus manifest.tsv (file<TAB>type<TAB>note).

These are known-answer vectors: the decoder must accept them and re-encode
byte-identically. P30 ledger-close fixtures are added once P30 activates;
today's live ledgers carry LedgerCloseMeta v1 (protocol 29).

Usage: tools/fetch_fixtures.py [--rpc URL] [--out tests/fixtures/xdr]
"""

from __future__ import annotations

import argparse
import json
import pathlib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_RPC = "https://soroban.stellar.org"


def rpc(url: str, method: str, params: dict | None = None) -> dict:
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method,
                       **({"params": params} if params else {})}).encode()
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/json",
                                          "User-Agent": "stellar-cpp-sdk-fixture-fetcher/0.1"})
    with urllib.request.urlopen(req, timeout=30) as r:
        out = json.loads(r.read())
    if "error" in out:
        raise RuntimeError(f"{method}: {out['error']}")
    return out["result"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rpc", default=DEFAULT_RPC)
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "tests/fixtures/xdr")
    ap.add_argument("--tx-count", type=int, default=4)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    manifest: list[tuple[str, str, str]] = []

    latest = rpc(args.rpc, "getLatestLedger")["sequence"]
    ledgers = rpc(args.rpc, "getLedgers", {"startLedger": latest - 1, "pagination": {"limit": 2}})
    for i, l in enumerate(ledgers["ledgers"]):
        meta = l.get("metadataXdr") or l.get("ledgerMetadata")
        if not meta:
            continue
        name = f"ledger_close_meta_{l['sequence']}.b64"
        (args.out / name).write_text(meta + "\n")
        manifest.append((name, "LedgerCloseMeta", f"ledger {l['sequence']} protocol {l.get('protocolVersion','?')}"))

    txs = rpc(args.rpc, "getTransactions",
              {"startLedger": latest - 3, "pagination": {"limit": args.tx_count}})
    got_soroban = False
    for i, t in enumerate(txs.get("transactions", [])[: args.tx_count]):
        h = t["txHash"][:12]
        for field, type_name, note in (
            ("envelopeXdr", "TransactionEnvelope", "envelope"),
            ("resultXdr", "TransactionResult", "result"),
            ("resultMetaXdr", "TransactionMeta", "meta"),
        ):
            val = t.get(field)
            if not val:
                continue
            name = f"tx_{note}_{h}.b64"
            (args.out / name).write_text(val + "\n")
            manifest.append((name, type_name, f"tx {t['txHash']} {note} ledger {t['ledger']}"))
        if not got_soroban and "soroban" in json.dumps(t).lower():
            got_soroban = True

    manifest.sort()
    lines = [f"{f}\t{t}\t{n}" for f, t, n in manifest]
    (args.out / "manifest.tsv").write_text("\n".join(lines) + "\n")
    print(f"wrote {len(manifest)} fixtures to {args.out} (soroban_seen={got_soroban})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
