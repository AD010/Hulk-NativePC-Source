#!/usr/bin/env python3
"""Synthetic, source-only tests for Hulk PC/Xbox UI resource audit."""
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent))
from audit_hulk_pc_x360_ui_assets import (
    audit_archive, collect_ui_tokens, compare_shared_records, parse_index,
    run, scan_ui_app, sha256_span,
)


def make_index(groups: list[tuple[str, list[tuple[int, int, int, int]]]],
               endian: str) -> bytes:
    """Make the original n=2*groups EoR name/table offset interleave."""
    count = 2 * len(groups)
    prefix = bytearray(4 + (count + 1)*4)
    struct.pack_into(endian + "I", prefix, 0, count)
    positions = []
    body = bytearray()
    for name, entries in groups:
        positions.append(len(prefix) + len(body))  # name
        body += name.encode("ascii") + b"\0"
        positions.append(len(prefix) + len(body))  # table
        body += struct.pack(endian + "I", len(entries))
        entries = sorted(entries)
        for key, _, _, _ in entries:
            body += struct.pack(endian + "I", key)
        for _, offset, size, reserved in entries:
            body += struct.pack(endian + "III", offset, size, reserved)
    positions.append(len(prefix) + len(body))
    body += struct.pack(endian + "I", 1000)
    struct.pack_into(endian + f"{len(positions)}I", prefix, 4, *positions)
    return bytes(prefix + body)


def make_data(*chunks: bytes, pad: int = 0) -> bytes:
    return b"\0" * 4 + b"".join(chunks) + b"\0" * pad


class TestHulkOriginalUiArchives(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)

    def write(self, name: str, content: bytes) -> Path:
        path = self.folder / name
        path.write_bytes(content)
        return path

    def test_pc_and_xbox_real_index_structure(self):
        pc = make_index([("Fonts", [(0xA79912FA, 4, 9, 0)]),
                         ("Textures", [(0x00112233, 4, 11, 0),
                                       (0xDDCCBBAA, 15, 12, 0)])], "<")
        xbox = make_index([("Fonts", [(0xA79912FA, 4, 12, 0)]),
                           ("Textures", [(0x00112233, 4, 11, 0),
                                         (0xFFEEDDCC, 15, 12, 0)])], ">")
        result_pc, result_x = parse_index(pc), parse_index(xbox)
        self.assertEqual(result_pc["endianness"], "little")
        self.assertEqual(result_x["endianness"], "big")
        self.assertEqual(result_pc["footer"], 1000)
        self.assertEqual(len(result_pc["groups"]["Textures"]), 2)
        self.assertEqual(result_x["groups"]["Fonts"][0]["hash"], "A79912FA")
        self.assertEqual(result_pc["groups"]["Textures"][1]["size"], 12)
        self.assertEqual(len(result_x["groups"]["Textures"]), 2)

    def test_rejects_bad_index_and_bad_archive_bounds(self):
        good = make_index([("Fonts", [(0xA79912FA, 4, 8, 0)])], "<")
        with self.assertRaises(ValueError):
            parse_index(good[:-1])
        with self.assertRaises(ValueError):
            parse_index(good[:8])
        with self.assertRaises(ValueError):
            parse_index(good[:-4] + b"\0\0\0\0\0\0\0\0")
        bad = make_index([("Fonts", [(0xA79912FA, 4, 900, 0)])], "<")
        path = self.write("FONTS.ARC", make_data(b"8bytes!!"))
        with self.assertRaises(ValueError):
            audit_archive(path, parse_index(bad)["groups"]["Fonts"])
        overlaps = [{"hash":"11111111","offset":4,"size":10},
                    {"hash":"22222222","offset":9,"size":10}]
        with self.assertRaises(ValueError):
            audit_archive(path, overlaps)

    def test_archive_sampled_ui_names_only_and_no_content_leak(self):
        data = make_data(b"TXFLui_menuitems_da\0 SECRET_API_TOKEN\0")
        f = self.write("TEXTURES.ARC", data)
        rows = [{"hash": "00112233", "offset": 4, "size": len(data)-4}]
        summary = audit_archive(f, rows)
        self.assertEqual(summary["resource_count"], 1)
        self.assertEqual(summary["last_indexed_end"], len(data))
        self.assertEqual(summary["sampled_prefix_ui_candidates"], [
            {"hash": "00112233", "ui_identifiers": ["txflui_menuitems_da"]}])
        self.assertNotIn("SECRET_API_TOKEN", json.dumps(summary))
        self.assertFalse(summary["probe_is_exhaustive"])

    def test_shared_hashes_byte_identity_vs_differing_font(self):
        pc = self.write("pc_fonts", make_data(b"FONT-CLEAN", b"DIFFER"))
        xbox = self.write("xbox_fonts", make_data(b"FONT-CLEAN", b"CHANGED"))
        a = [{"hash":"A79912FA","offset":4,"size":10},
             {"hash":"B0000001","offset":14,"size":6}]
        b = [{"hash":"A79912FA","offset":4,"size":10},
             {"hash":"B0000001","offset":14,"size":7}]
        pa = audit_archive(pc, a)
        xa = audit_archive(xbox, b)
        self.assertEqual(pa["resource_count"], 2)
        self.assertEqual(xa["resource_count"], 2)
        match = compare_shared_records(pc, xbox, a, b)
        self.assertEqual(match["shared_hashes"], 2)
        self.assertEqual(match["byte_identical"], 1)
        self.assertEqual(match["payloads_different"], 1)

    def test_complete_offline_dual_platform_audit(self):
        pc_index = self.write("indexPC.ind", make_index([
            ("Fonts", [(0xA79912FA, 4, 12, 0)]),
            ("Textures", [(0x10000001, 4, 18, 0)])], "<"))
        x_index = self.write("indexXBOX360.ind", make_index([
            ("Fonts", [(0xA79912FA, 4, 9, 0)]),
            ("Textures", [(0x10000001, 4, 18, 0),
                          (0x10000002, 22, 4, 0)])], ">"))
        pf = self.write("pcfont.arc", make_data(b"A" * 12, pad=3))
        xf = self.write("xfont.arc", make_data(b"B" * 9, pad=4))
        pt = self.write("pctextures.arc", make_data(b"TXFLui_menuitems_da"))
        xt = self.write("xtextures.arc", make_data(b"TXFLui_menuitems_da",
                                                  b"TEST"))
        app = self.write("_app_pc", b"UIPage_Pause_Settings\0"
                         b"UI_OptionsReturn\0 other data")
        summary = run(pc_index, x_index,
                      {"Fonts":pf,"Textures":pt},
                      {"Fonts":xf,"Textures":xt},
                      pc_app=app, xbox_app=app)
        self.assertEqual(summary["group_counts"]["Textures"],
                         {"pc":1,"xbox":2})
        self.assertEqual(summary["resource_comparisons"]["Textures"]
                         ["byte_identical"],1)
        self.assertEqual(summary["resource_comparisons"]["Fonts"]
                         ["payloads_different"],1)
        self.assertIn("uipage_pause_settings",
                      summary["shared_ui_identifiers"])
        self.assertFalse(summary["game_bytes_exported"])
        self.assertFalse(summary["original_ui_hooked"])

    def test_nearby_hash_is_tentative_eor_ui_resource_link(self):
        # A texture-hash dword next to an authored UI string is a useful
        # candidate, never a proven widget pointer or ABI call.
        from audit_hulk_pc_x360_ui_assets import scan_ui_app
        tex_hash = 0x11223344
        text = b"UIPage_Pause_Settings\0"
        pc = self.write("pc_decoded_app", text + tex_hash.to_bytes(4, "little"))
        xbox = self.write("xbox_decoded_app", text + tex_hash.to_bytes(4, "big"))
        rows = {"Textures":[{"hash":"11223344","offset":4,"size":8}]}
        for path, byte_order in ((pc,"little"),(xbox,"big")):
            info = scan_ui_app(path, rows, byte_order)
            candidates = info["index_hash_proximity_candidates"]
            self.assertEqual(len(candidates), 1)
            self.assertEqual(candidates[0]["indexed_hash"], "11223344")
            self.assertEqual(candidates[0]["ui_identifier"],
                             "uipage_pause_settings")
            self.assertFalse(candidates[0]["verified_link"])
            self.assertFalse(info["nearby_hashes_are_verified_bindings"])
            self.assertNotIn("UIPage_Pause_Settings", json.dumps(info))

    def test_undecoded_app_name_finding_is_not_resource_decoding(self):
        from audit_hulk_pc_x360_ui_assets import scan_ui_app
        app = self.write("app", b"some binary prefix\0"
                         b"TXFLui_menuitems_da\0bogus")
        info = scan_ui_app(app)
        self.assertIn("txflui_menuitems_da", info["ui_identifiers"])
        self.assertNotIn("index_hash_proximity_candidates", info)
        self.assertFalse(info["nearby_hashes_are_verified_bindings"])

    def test_large_tail_suggests_wrong_platform_not_proof(self):
        f = self.write("claimed_pc_objects.arc", make_data(b"1234",pad=100))
        info = audit_archive(f, [{"hash":"A","offset":4,"size":4}])
        self.assertIsNotNone(info["warning"])
        self.assertEqual(info["resource_count"],1)


if __name__ == "__main__":
    unittest.main()
