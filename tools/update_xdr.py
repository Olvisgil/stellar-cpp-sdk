#!/usr/bin/env python3
# Copyright 2026 The stellar-cpp-sdk Authors
# SPDX-License-Identifier: Apache-2.0
"""Fetch and verify the pinned Stellar XDR definitions.

The SDK's XDR types are generated from ``xdr/xdr.json``, the resolved XDR AST
published by SDF in ``stellar/rs-stellar-xdr``. The original ``.x`` sources from
``stellar/stellar-xdr`` are vendored next to it for reference and review.

Usage:
  tools/update_xdr.py --check                 verify vendored files (offline)
  tools/update_xdr.py --tag v30.0.0           fetch a release and rewrite xdr/
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
XDR_DIR = ROOT / "xdr"
VERSION_FILE = XDR_DIR / "VERSION"
RS_REPO = "stellar/rs-stellar-xdr"
XDR_REPO = "stellar/stellar-xdr"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalized_sha256(data: bytes) -> str:
    """Hash a .x file the way rs-stellar-xdr's xdr-parser records it in xdr.json.

    Mirrors ``strip_ifdef_blocks_and_whitespace`` in
    ``xdr-generator-rust/xdr-parser/src/parser.rs``: lines between ``#ifdef`` and
    ``#else``/``#endif`` are dropped along with the directives themselves, then
    all whitespace is removed before hashing.
    """
    kept: list[str] = []
    skip = False
    for line in data.decode("utf-8").split("\n"):
        line = line.removesuffix("\r")
        trimmed = line.lstrip()
        if trimmed.startswith("#ifdef"):
            skip = True
        elif trimmed.startswith("#else") or trimmed.startswith("#endif"):
            skip = False
        elif not skip:
            kept.append(line)
    return sha256("".join(ch for ch in "".join(kept) if not ch.isspace()).encode("utf-8"))


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "stellar-cpp-sdk-update-xdr"})
    with urllib.request.urlopen(request, timeout=60) as response:  # noqa: S310 - fixed https URLs
        return response.read()


def read_version() -> dict[str, str]:
    entries: dict[str, str] = {}
    for line in VERSION_FILE.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, _, value = line.partition("=")
            entries[key.strip()] = value.strip()
    return entries


def verify(xdr_json: bytes, sources: dict[str, bytes], expected_json_sha: str | None) -> list[str]:
    problems: list[str] = []
    if expected_json_sha and sha256(xdr_json) != expected_json_sha:
        problems.append(f"xdr.json sha256 {sha256(xdr_json)} != pinned {expected_json_sha}")
    ast = json.loads(xdr_json)
    if ast.get("version") != 1:
        problems.append(f"unsupported xdr.json format version {ast.get('version')!r}")
    listed = {entry["name"]: entry["sha256"] for entry in ast["files"]}
    for name, digest in listed.items():
        data = sources.get(name)
        if data is None:
            problems.append(f"missing source {name}")
        elif normalized_sha256(data) != digest:
            problems.append(f"{name}: normalized sha256 {normalized_sha256(data)} != {digest} recorded in xdr.json")
    for name in sources:
        if name not in listed:
            problems.append(f"unexpected source {name} not listed in xdr.json")
    return problems


def check() -> int:
    version = read_version()
    xdr_json = (XDR_DIR / "xdr.json").read_bytes()
    sources = {f"xdr/{path.name}": path.read_bytes() for path in sorted(XDR_DIR.glob("*.x"))}
    problems = verify(xdr_json, sources, version.get("xdr-json-sha256"))
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    if not problems:
        print(f"ok: xdr.json and {len(sources)} .x files match {version.get('rs-stellar-xdr-tag')}")
    return 1 if problems else 0


def update(tag: str, protocol: str) -> int:
    ref = json.loads(fetch(f"https://api.github.com/repos/{RS_REPO}/git/refs/tags/{tag}"))
    rs_commit = ref["object"]["sha"]
    xdr_commit = fetch(f"https://raw.githubusercontent.com/{RS_REPO}/{rs_commit}/xdr-version").decode().strip()
    xdr_json = fetch(f"https://raw.githubusercontent.com/{RS_REPO}/{rs_commit}/xdr-definitions-json/xdr.json")
    ast = json.loads(xdr_json)
    sources = {
        entry["name"]: fetch(f"https://raw.githubusercontent.com/{XDR_REPO}/{xdr_commit}/{entry['name'].removeprefix('xdr/')}")
        for entry in ast["files"]
    }
    problems = verify(xdr_json, sources, None)
    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1
    for old in XDR_DIR.glob("*.x"):
        old.unlink()
    (XDR_DIR / "xdr.json").write_bytes(xdr_json)
    for name, data in sources.items():
        (ROOT / name).write_bytes(data)
    VERSION_FILE.write_text(
        "# Pinned Stellar XDR inputs. Update with tools/update_xdr.py --tag <rs-stellar-xdr tag>.\n"
        f"protocol={protocol}\n"
        f"rs-stellar-xdr-tag={tag}\n"
        f"rs-stellar-xdr-commit={rs_commit}\n"
        f"stellar-xdr-commit={xdr_commit}\n"
        f"xdr-json-sha256={sha256(xdr_json)}\n"
    )
    print(f"updated xdr/ to {tag} ({rs_commit[:12]}), stellar-xdr {xdr_commit[:12]}, {len(sources)} files")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true", help="verify the vendored files without network access")
    group.add_argument("--tag", help="rs-stellar-xdr release tag to vendor, e.g. v30.0.0")
    parser.add_argument("--protocol", help="protocol version the release targets (required with --tag)")
    args = parser.parse_args()
    if args.check:
        return check()
    if not args.protocol:
        parser.error("--protocol is required with --tag")
    return update(args.tag, args.protocol)


if __name__ == "__main__":
    sys.exit(main())
