#!/usr/bin/env python3
"""Regression guard for the original Hulk XDK 6534 Present/Swap calling convention.

The verified USA/EU PPC calls the lower-level Swap with a front-buffer pointer
in r4, while the outer Present wrapper takes only the device in r3.
NativeXRenderer's Present capture reads r4 and must use the inner Swap.
This checks the profile only; real frame output still requires a gameplay test.
"""
from configparser import ConfigParser
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "native_port" / "hulk_native_x_profile.ini"

class TestHulkPresentABI(unittest.TestCase):
    def test_only_frame_end_has_correct_front_buffer_argument(self):
        config = ConfigParser(interpolation=None, strict=True)
        config.optionxform = str
        with PROFILE.open(encoding="utf-8") as stream:
            config.read_file(stream)
        hooks = dict(config.items("hooks"))
        self.assertEqual(hooks.get("0x82880778"), "Present")
        self.assertNotIn("0x82880D10", hooks)
        self.assertEqual(sum(role == "Present" for role in hooks.values()), 1)
        self.assertEqual(hooks.get("0x8288DF30"), "DrawVertices")
        self.assertEqual(hooks.get("0x8288E318"), "DrawIndexedVertices")
        self.assertEqual(hooks.get("0x828897F8"), "Resolve")
        self.assertEqual(hooks.get("0x8287F2C0"), "SetRenderTarget")

if __name__ == "__main__":
    unittest.main()
