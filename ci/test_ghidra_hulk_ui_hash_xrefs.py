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

    def test_original_pause_settings_searches_all_actual_text_encodings(self):
        # Run the source helper without importing Ghidra/Jython globals.
        parsed = ast.parse(SCRIPT.read_text(encoding="utf-8"))
        helper = next(node for node in parsed.body
                      if isinstance(node, ast.FunctionDef) and
                      node.name == "identifier_byte_signatures")
        isolated = ast.Module(body=[helper], type_ignores=[])
        namespace = {}
        exec(compile(isolated, str(SCRIPT), "exec"), namespace)
        name = "UIPage_Pause_Settings"
        signatures = dict(namespace["identifier_byte_signatures"](name))
        self.assertEqual(set(signatures), {"ASCII", "UTF-16LE", "UTF-16BE"})
        self.assertEqual(signatures["ASCII"], name.encode("ascii"))
        self.assertEqual(signatures["UTF-16LE"], name.encode("utf-16-le"))
        self.assertEqual(signatures["UTF-16BE"], name.encode("utf-16-be"))
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertIn("for address, encoding in search_identifier_encodings(program, name):",
                      source)
        self.assertIn('"string_encoding": encoding', source)
        self.assertIn("remaining = MAX_MATCHES - len(matches)", source)
        # Call edges come from Ghidra's typed call references, never a
        # guessed guest address derived from a string or resource CRC.
        self.assertIn("def bounded_direct_callers(func, program):", source)
        self.assertIn("ref.getReferenceType().isCall()", source)
        self.assertIn('"direct_caller_candidates": bounded_direct_callers(func, program)', source)
        self.assertIn('"guest_callback_abi_proven": False', source)

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
