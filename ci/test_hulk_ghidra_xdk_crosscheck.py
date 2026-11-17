#!/usr/bin/env python3
"""Synthetic address-only proof regression; never commit original Hulk bytes."""
import copy
import unittest

from hulk_engine_render_callgraph import USA_XEX_SHA256
from hulk_ghidra_xdk_crosscheck import compare

GHIDRA = {
    "schema": "hulk_ghidra_usa_xdk_direct_v1",
    "source_xex_sha256": USA_XEX_SHA256,
    "powerpc_import_verified": True,
    "engine_draw_abi_verified": False,
    "edges": [{
        "xdk_guest_address": "0x8288E318",
        "verified_xdk_method": "DrawIndexedVertices",
        "static_direct_bl_site": "0x82500020",
        "observed_guest_lr": "0x82500024",
        "source_defined_guest_caller": "0x82500000",
        "direct_bl": True,
        "engine_draw_abi_verified": False,
    }],
}
RUNTIME = {
    "schema": "hulk_usa_runtime_to_static_xdk_callsite_v1",
    "source_xex_sha256": USA_XEX_SHA256,
    "matched": [{
        "xdk_guest_address": "0x8288E318",
        "verified_xdk_method": "DrawIndexedVertices",
        "static_direct_bl_site": "0x82500020",
        "observed_guest_lr": "0x82500024",
        "source_defined_guest_callers": ["0x82500000"],
        "verified_direct_bl_link": True,
        "verified_engine_draw_abi": False,
    }],
}

class TriplePPCWitness(unittest.TestCase):
    def test_exact_ghidra_codegen_runtime_direct_bl(self):
        result = compare(GHIDRA, RUNTIME)
        self.assertEqual(result["triple_witnessed_direct_bl_count"], 1)
        self.assertEqual(result["runtime_verified_but_ghidra_unresolved_count"], 0)
        self.assertTrue(result["triple_witnesses"][0]["source_bl_and_runtime_lr_triple_verified"])
        self.assertFalse(result["triple_witnesses"][0]["engine_draw_abi_verified"])
        self.assertFalse(result["native_engine_draw_bridge_ready"])

    def test_ghidra_does_not_promote_unmatched_source_sites(self):
        g = copy.deepcopy(GHIDRA)
        g["edges"][0]["source_defined_guest_caller"] = "0x82500010"
        result = compare(g, RUNTIME)
        self.assertEqual(result["triple_witnessed_direct_bl_count"], 0)
        self.assertEqual(result["runtime_verified_but_ghidra_unresolved_count"], 1)

    def test_pal_and_untrusted_bytecode_never_accepted(self):
        for field,value in [("source_xex_sha256", "0"*64),
                            ("powerpc_import_verified", False),
                            ("engine_draw_abi_verified", True)]:
            g = copy.deepcopy(GHIDRA)
            g[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                compare(g, RUNTIME)

    def test_false_edges_and_fabricated_abi_rejected(self):
        for field,value in [("direct_bl", False),
                            ("engine_draw_abi_verified", True),
                            ("observed_guest_lr", "0x82500028"),
                            ("verified_xdk_method", "Present"),
                            ("static_direct_bl_site", "0x81000000")]:
            g = copy.deepcopy(GHIDRA)
            g["edges"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                compare(g, RUNTIME)

    def test_duplicate_ghidra_edge_rejected(self):
        g = copy.deepcopy(GHIDRA)
        g["edges"].append(copy.deepcopy(g["edges"][0]))
        with self.assertRaises(ValueError):
            compare(g, RUNTIME)

    def test_unverified_runtime_abi_and_invalid_lr_rejected(self):
        for field,value in [("verified_engine_draw_abi", True),
                            ("observed_guest_lr", "0x8250001C"),
                            ("verified_xdk_method", "DrawVertices")]:
            r = copy.deepcopy(RUNTIME)
            r["matched"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                compare(GHIDRA, r)

    def test_empty_unanalyzed_ghidra_is_not_success(self):
        g = copy.deepcopy(GHIDRA)
        g["edges"] = []
        result = compare(g, RUNTIME)
        self.assertEqual(result["triple_witnessed_direct_bl_count"], 0)
        self.assertEqual(len(result["unresolved"]), 1)

if __name__ == "__main__":
    unittest.main()
