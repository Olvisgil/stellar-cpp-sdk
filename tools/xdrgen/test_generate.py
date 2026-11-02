#!/usr/bin/env python3
# Copyright 2026 The stellar-cpp-sdk Authors
# SPDX-License-Identifier: Apache-2.0
"""Unit tests for tools/xdrgen/generate.py. Run: python3 test_generate.py"""

from __future__ import annotations

import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import generate  # noqa: E402


def defn(kind, name, file_index=0, source="", **data):
    d = {"kind": kind, "name": name, "file_index": file_index}
    d.update(data)
    d["source"] = source
    return d


def small_ast(defs):
    return {"files": [{"name": "xdr/t.x", "sha256": "0" * 64}], "definitions": defs}


I32 = {"kind": "ref", "name": "int32"}
INT_T = {"kind": "int"}


class TestNaming(unittest.TestCase):
    def test_camelize(self):
        self.assertEqual(generate.camelize("SCP_ST_PREPARE"), "scpStPrepare")
        self.assertEqual(generate.camelize("KEY_TYPE_ED25519"), "keyTypeEd25519")
        self.assertEqual(generate.camelize("LIVEENTRY"), "liveentry")
        self.assertEqual(generate.camelize("MEMO_TEXT"), "memoText")

    def test_ident_keyword(self):
        self.assertEqual(generate.ident("case"), "case_")
        self.assertEqual(generate.ident("switch"), "switch_")
        self.assertEqual(generate.ident("normal"), "normal")


class TestGraph(unittest.TestCase):
    def test_typedef_resolution(self):
        g = generate.Gen(small_ast([
            defn("struct", "Key", fields=[]),
            defn("typedef", "Alias", type={"kind": "ref", "name": "Key"}),
            defn("typedef", "Alias2", type={"kind": "ref", "name": "Alias"}),
        ]))
        self.assertEqual(g.resolve_ref("Alias2").name, "Key")
        self.assertEqual(g.final_kind("Alias"), "struct")

    def test_self_cycle_boxes_optional(self):
        # struct Node { Node* next; } -> OptionalBox on the field edge
        g = generate.Gen(small_ast([
            defn("struct", "Node",
                 fields=[{"name": "next", "type": {"kind": "optional",
                                                   "element": {"kind": "ref", "name": "Node"}}}]),
        ]))
        self.assertIn(("Node", "field", 0), g.boxed_edges)
        self.assertEqual(g.boxed_edges[("Node", "field", 0)], "optional")

    def test_union_vec_self_cycle_boxes(self):
        # union U { case A: U v<>; } -> Box<vector<U>>
        g = generate.Gen(small_ast([
            defn("enum", "D", members=[{"name": "A", "value": 0}]),
            defn("union", "U",
                 discriminant={"name": "type", "type": {"kind": "ref", "name": "D"}},
                 arms=[{"cases": [{"name": "A", "value": 0}], "name": "v",
                        "type": {"kind": "var_array", "element": {"kind": "ref", "name": "U"}}}]),
        ]))
        self.assertEqual(g.boxed_edges.get(("U", "arm", 0)), "vec")

    def test_no_cycle_no_boxing(self):
        g = generate.Gen(small_ast([
            defn("struct", "A", fields=[]),
            defn("struct", "B",
                 fields=[{"name": "a", "type": {"kind": "ref", "name": "A"}},
                         {"name": "xs", "type": {"kind": "var_array",
                                                 "element": {"kind": "ref", "name": "A"}}}]),
        ]))
        self.assertEqual(g.boxed_edges, {})
        self.assertLess(g.emit_order.index("A"), g.emit_order.index("B"))

    def test_min_sizes(self):
        g = generate.Gen(small_ast([
            defn("struct", "S",
                 fields=[{"name": "a", "type": INT_T},
                         {"name": "b", "type": {"kind": "opaque_var", "max_size": 64}}]),
            defn("union", "U",
                 discriminant={"name": "type", "type": INT_T},
                 arms=[{"cases": [{"value": 0}], "name": None, "type": None},
                       {"cases": [{"value": 1}], "name": "s",
                        "type": {"kind": "ref", "name": "S"}}]),
        ]))
        # struct S: int(4) + opaque_var(4) = 8
        self.assertEqual(g.min_size["S"], 8)
        # union U: disc(4) + min(void arm 0, S 8) = 4
        self.assertEqual(g.min_size["U"], 4)


class TestRealAst(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ast = json.loads((generate.XDR_JSON).read_text())
        cls.g = generate.Gen(cls.ast)
        cls.g._ast_files = [f["name"] for f in cls.ast["files"]]

    def test_definition_census(self):
        kinds = {}
        for d in self.g.defs:
            kinds[d.kind] = kinds.get(d.kind, 0) + 1
        self.assertEqual(kinds, {"struct": 237, "union": 115, "enum": 85,
                                 "typedef": 40, "const": 18})

    def test_emit_order_covers_and_orders(self):
        order = self.g.emit_order
        self.assertEqual(len(order), len(set(order)))
        self.assertEqual(set(order), set(self.g.structs) | set(self.g.unions))
        # every non-boxed member edge is satisfied: target precedes parent
        pos = {n: i for i, n in enumerate(order)}
        for name, d in {**self.g.structs, **self.g.unions}.items():
            src = d.data
            key = "field" if "fields" in src else "arm"
            mems = src["fields"] if "fields" in src else src["arms"]
            for i, m in enumerate(mems):
                if not m.get("type"):
                    continue  # void arm: nothing to order
                for t, _kind in self.g._member_edges(m["type"]):
                    if (name, key, i) in self.g.boxed_edges:
                        continue  # Box only needs a forward decl
                    self.assertLess(pos[t], pos[name], f"{t} must precede {name}")

    def test_known_cycles_boxed(self):
        g = self.g
        # ClaimPredicate recurses through optional + vector arms
        self.assertIn(("ClaimPredicate", "arm", 3), g.boxed_edges)  # notPredicate*
        self.assertEqual(g.boxed_edges[("ClaimPredicate", "arm", 3)], "optional")
        self.assertIn(("ClaimPredicate", "arm", 1), g.boxed_edges)  # andPredicates<>
        self.assertEqual(g.boxed_edges[("ClaimPredicate", "arm", 1)], "vec")
        # SCVal <-> SCMapEntry recursion is boxed somewhere
        scval_boxes = [k for k in g.boxed_edges if k[0] in ("SCVal", "SCMapEntry")]
        self.assertTrue(scval_boxes)

    def test_no_name_collisions(self):
        # every emitted C++ identifier is legal (no keyword clash unhandled)
        for d in self.g.defs:
            self.assertNotIn(generate.ident(d.name), generate.CPP_KEYWORDS)
            if d.kind == "struct":
                for f in d.data["fields"]:
                    self.assertNotIn(generate.ident(f["name"]), generate.CPP_KEYWORDS)

    def test_file_headers_dag(self):
        deps = self.g._file_deps()
        # topological sort must succeed (no cycles)
        pending = set(range(len(self.ast["files"])))
        done: set[int] = set()
        while pending:
            ready = [f for f in pending if deps.get(f, set()) <= done]
            self.assertTrue(ready, "file dependency cycle")
            done.update(ready)
            pending -= set(ready)

    def test_emitted_files(self):
        files = self.g.emit()
        self.assertIn(generate.OUT_HEADER, files)
        self.assertEqual(len([p for p in files if p.suffix == ".hpp"]), 14)
        self.assertEqual(len([p for p in files if p.suffix == ".cpp"]), 14)


if __name__ == "__main__":
    unittest.main(verbosity=1)
