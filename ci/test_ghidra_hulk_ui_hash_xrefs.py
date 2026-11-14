#!/usr/bin/env python3
"""Source-only regression for original EoR UI hash-provenance Ghidra scan."""
import ast
import csv
from pathlib import Path
import re
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "ci" / "ghidra_hulk_ui_xrefs.py"
MAP = ROOT / "native_port" / "analysis" / "eor_ui_original_resource_map.csv"


class EorUiHashXrefsTests(unittest.TestCase):
    def test_known_resource_hashes_match_actual_retail_index_records(self):
        parsed = ast.parse(SCRIPT.read_text(encoding="utf-8"))
        assignment = next(node for node in parsed.body
                          if isinstance(node, ast.Assign)
                          and any(isinstance(t, ast.Name) and
                                  t.id == "VERIFIED_UI_RESOURCE_HASHES"
                                  for t in node.targets))
        targets = ast.literal_eval(assignment.value)
        self.assertEqual(len(targets), 13)
        with MAP.open(newline="", encoding="utf-8") as fp:
            rows = list(csv.DictReader(fp))
        for group, name, value in targets:
            self.assertEqual(zlib.crc32(name.upper().encode("ascii")) & 0xffffffff, value)
            matches = [row for row in rows
                       if row["group"] == group and
                       row["canonical_resource_name"] == name]
            self.assertTrue(matches, (group, name))
            self.assertTrue(all(int(row["crc32_upper_hash"], 16) == value
                                for row in matches))

    def test_menu_xref_search_does_not_claim_abi_or_embed_game_data(self):
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertIn("def search_exact_binary(", source)
        self.assertIn("ord(c) - 256", source)
        self.assertIn('struct.pack(endian, expected_crc)', source)
        self.assertIn('"constant_byte_order": order', source)
        self.assertIn('"candidate_only": True', source)
        self.assertIn('"proves_menu_callback_or_guest_abi": False', source)
        self.assertIn('"xrefs_are_abi_proof": False', source)
        self.assertIn('"original_ui_hash_constant_candidates"', source)
        self.assertNotIn("PPCContext", source)
        self.assertNotIn("0x8211", source)


if __name__ == "__main__":
    unittest.main()
