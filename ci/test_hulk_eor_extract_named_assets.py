#!/usr/bin/env python3
"""Verified EoR CRC32 resource extraction; synthetic archives, no game bytes."""
from __future__ import annotations

import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent))
from audit_hulk_pc_x360_ui_assets import parse_index
from hulk_eor_extract_named_assets import extract, resource_hash, selector
from test_audit_hulk_pc_x360_ui_assets import make_index, make_data


class TestEorNamedResourceExtraction(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def write(self, name: str, content: bytes) -> Path:
        p = self.root / name
        p.write_bytes(content)
        return p

    def test_actual_pc_xbox_index_name_hashes(self):
        # ALL these exact hashes have been matched in user-owned,
        # independently decoded PC/Xbox index.ind JSON. Not guesses.
        expected = {
            "_app_": "721AAFDE",
            "_engine_": "CA54CBC0",
            "_shader_effects_": "2485AB96",
            "font_clean": "A796ECFA",
            "ui_menuitems_da": "2F95A2B1",
            "ui_menubands_da": "20CE9F3C",
            "ui_key_wasd_da": "6274200A",
            "ui_xbox360_a_da": "16FEB6A7",
            "ui_mouse_leftbutton_da": "79FFD3E8",
        }
        for original_name, observed_hash in expected.items():
            self.assertEqual(resource_hash(original_name), observed_hash)
        for name in ("ui_menuitems_da", "ui_menubands_da",
                     "ui_key_wasd_da", "ui_mouse_leftbutton_da"):
            self.assertEqual(resource_hash("TXFL" + name),
                             resource_hash(name))
        self.assertEqual(resource_hash("LFXTui_xbox360_a_da"),
                         resource_hash("ui_xbox360_a_da"))
        with self.assertRaises(ValueError):
            resource_hash("../ui_menuitems_da")
        with self.assertRaises(ValueError):
            resource_hash("path\\ui_menuitems_da")

    def test_extract_font_and_glyph_by_actual_hash(self):
        font_bytes = b"EOR-FONT-A"
        texture_bytes = b"EOR-TEXTURE-SYNTHETIC"
        records = [
            ("Fonts", [(int(resource_hash("font_clean"), 16), 4,
                        len(font_bytes), 0)]),
            ("Textures", [(int(resource_hash("ui_xbox360_a_da"), 16), 4,
                           len(texture_bytes), 0)]),
        ]
        index = self.write("indexPC.ind", make_index(records, "<"))
        font = self.write("FONTS.ARC", make_data(font_bytes))
        tex = self.write("TEXTURES.ARC", make_data(texture_bytes))
        out = self.root / "private_output"
        manifest = extract(index, {"Fonts": font, "Textures": tex}, [
            ("Fonts", "font_clean"),
            ("Textures", "LFXTui_xbox360_a_da"),
        ], out)
        self.assertEqual(manifest["endianness"], "little")
        self.assertEqual(len(manifest["records"]), 2)
        self.assertEqual((out / "Fonts" / "A796ECFA.bin").read_bytes(),
                         font_bytes)
        self.assertEqual((out / "Textures" / "16FEB6A7.bin").read_bytes(),
                         texture_bytes)
        self.assertEqual(manifest["records"][0]["sha256"],
                         hashlib.sha256(font_bytes).hexdigest())
        self.assertFalse(manifest["records"][0]["payload_format_verified"])
        self.assertNotIn("EOR-FONT-A", str(manifest))
        self.assertFalse(manifest["asset_bytes_embedded"])

    def test_xbox_big_endian_and_bad_size_fail_closed(self):
        index = self.write("indexXBOX360.ind", make_index([
            ("Fonts", [(int(resource_hash("font_clean"), 16), 4, 7, 0)])
        ], ">"))
        archive = self.write("FONTS.ARC", make_data(b"abcd"))
        self.assertEqual(parse_index(index.read_bytes())["endianness"], "big")
        with self.assertRaisesRegex(ValueError, "Out-of-bounds"):
            extract(index, {"Fonts": archive},
                    [("Fonts", "font_clean")], self.root / "private_xbox")
        with self.assertRaises(LookupError):
            extract(index, {"Fonts": archive},
                    [("Fonts", "font_missing")], self.root / "private_xbox")

    def test_rejects_unsafe_name_and_missing_category(self):
        with self.assertRaises(Exception):
            selector("Fonts:../../font_clean")
        index = self.write("indexPC.ind", make_index([
            ("Fonts", [(int(resource_hash("font_clean"), 16), 4, 3, 0)])
        ], "<"))
        source = self.write("font", make_data(b"abc"))
        with self.assertRaisesRegex(ValueError, "Missing private"):
            extract(index, {}, [("Fonts", "font_clean")],
                    self.root / "out")
        with self.assertRaisesRegex(ValueError, "Archive category"):
            extract(index, {"Fonts": source},
                    [("Textures", "ui_menuitems_da")], self.root / "out")
        with self.assertRaisesRegex(ValueError, "Unsupported resource size"):
            extract(index, {"Fonts": source},
                    [("Fonts", "font_clean")], self.root / "out",
                    max_resource=2)


if __name__ == "__main__":
    unittest.main()
