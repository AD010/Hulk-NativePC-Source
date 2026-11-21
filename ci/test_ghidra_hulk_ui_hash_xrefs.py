#!/usr/bin/env python3
"""Source-only regression for original EoR UI hash-provenance Ghidra scan."""
import ast
import csv
import json
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

    def test_original_vm_pause_method_hashes_and_guest_ppc_literal_pattern(self):
        source = SCRIPT.read_text(encoding="utf-8")
        parsed = ast.parse(source)
        assignment = next(node for node in parsed.body
                          if isinstance(node, ast.Assign)
                          and any(isinstance(t, ast.Name)
                                  and t.id == "PAUSE_VM_METHOD_HASHES"
                                  for t in node.targets))
        verified = dict(ast.literal_eval(assignment.value))
        self.assertEqual(len(verified), 7)
        for name, original_crc in verified.items():
            self.assertEqual(zlib.crc32(name.upper().encode("ascii")) & 0xffffffff,
                             original_crc)
        method = next(node for node in parsed.body
                      if isinstance(node, ast.FunctionDef) and
                      node.name == "ppc_lis_ori_words")
        isolated = ast.Module(body=[method], type_ignores=[])
        ns = {}
        exec(compile(isolated, str(SCRIPT), "exec"), ns)
        load = ns["ppc_lis_ori_words"]
        # These are actual big-endian PowerPC instruction words for
        # lis r3,0xF7A5 / ori r3,r3,0xB1D1; they do not prove a caller.
        self.assertEqual(load(0xF7A5B1D1, 3),
                         (0x3C60F7A5, 0x6063B1D1))
        for value, reg in [(-1, 3), (0x100000000, 3), (0, 0), (0, 32)]:
            with self.assertRaises(ValueError):
                load(value, reg)
        reference = json.loads((ROOT / "native_port/analysis/"
                               "eor_original_pause_ui_source_witness_2026-11-21.json")
                               .read_text(encoding="utf-8"))
        for platform in ("xbox360_object_archive", "pc_2008_comparator"):
            original = reference["sources"][platform]
            self.assertEqual(original["change_state_vm_opaque_0x28_operand"],
                             "F7A5B1D1")
            for name in ("ChangeState_r", "Init_z", "uiwidget_pausemenu_z"):
                self.assertIn(name, original["pause_vm_method_payload_lengths"])
        self.assertIn('"original_pause_vm_xex_constant_candidates"', source)
        self.assertIn('"PowerPC_lis_ori"', source)
        self.assertIn('"proves_original_pause_event_handler": False', source)

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
