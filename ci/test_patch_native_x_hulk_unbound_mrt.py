#!/usr/bin/env python3
"""Protect Hulk RT0/depth and previously observed MRT slots from silent loss."""
import unittest
from patch_native_x_hulk_packet_surfaces import EDITS
from patch_native_x_hulk_unbound_mrt import OLD, NEW, patch_text

class HulkUnboundMRTTest(unittest.TestCase):
    def test_real_preceding_surface_patch_anchor(self):
        source = next(e["new"] for e in EDITS
                      if e["path"] == "src/renderer.cpp"
                      and "bool HulkPacketSurfaces(" in e["new"])
        self.assertEqual(source.count(OLD), 1)
        result, changed = patch_text(source)
        self.assertTrue(changed)
        self.assertIn("HULK/UNBOUND-MRT", result)
        self.assertIn("g_hulk_surface_history[slot].empty()", result)
        # RT0 and depth must still reject missing surface identity.
        self.assertIn("if (slot > 0 && slot < 4 && !bound)", result)
        self.assertIn("if (!has_target || unresolved)", result)
        self.assertEqual(patch_text(result), (result, False))

    def test_partial_or_wrong_source_fails_closed(self):
        with self.assertRaises(ValueError):
            patch_text("bool HulkPacketSurfaces() { }")

if __name__ == "__main__":
    unittest.main()
