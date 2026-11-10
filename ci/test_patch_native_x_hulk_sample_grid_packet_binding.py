#!/usr/bin/env python3
"""Source-only regression tests for the exact Hulk XDK/PM4 sample-grid pair."""
import unittest
from patch_native_x_hulk_sample_grid_packet_binding import OLD, NEW, patch_text

class HulkPacketSampleViewTests(unittest.TestCase):
    def test_exact_source_and_idempotency(self):
        source = "bool HulkPacketSurfaces(\n" + OLD
        out, changed = patch_text(source)
        self.assertTrue(changed)
        self.assertEqual(patch_text(out), (out, False))
        with self.assertRaises(ValueError):
            patch_text("bool HulkPacketSurfaces(\nchanged upstream")

    def test_exact_real_view_only(self):
        for expected in (
            "bound.width == 1280 && bound.height == 720",
            "bound.pitch() == 1280 && bound.msaa() == 0",
            "(gpu_surface & 0x3FFFu) == 640u",
            "((gpu_surface >> 16) & 3u) == 2u",
            "(gpu_info & 0x000F0FFFu) == (bound.info & 0x000F0FFFu)",
            "packet_view = bound", "packet_view.surface_info = gpu_surface",
            "HulkRememberSurface(packet_view, slot);",
        ):
            self.assertIn(expected, NEW)
        self.assertIn("[HULK/PM4-SAMPLE-VIEW]", NEW)
        self.assertIn("[HULK/PM4-HISTORY-ALIAS]", NEW)
        for checked in (
            "const HulkSurfaceIdentity xdk_key{1280u, gpu_info & 0x000F0FFFu};",
            "g_hulk_surface_history[slot].find(xdk_key)",
            "known.depth == (slot == 4)",
            "known.pitch() == 1280 && known.msaa() == 0",
            "known.width == 1280 && known.height == 720",
            "(known.info & 0x000F0FFFu) == (gpu_info & 0x000F0FFFu)",
            "packet_view = verified",
            "verified.object",
        ):
            self.assertIn(checked, NEW)
        self.assertIn("continue;", NEW)

    def test_logged_surface_identity(self):
        gpu_surface = 0x0A020280
        bound_surface = 0x14000500
        gpu_info = bound_info = 0x400
        self.assertEqual(gpu_surface & 0x3FFF, 640)
        self.assertEqual((gpu_surface >> 16) & 3, 2)
        self.assertEqual(bound_surface & 0x3FFF, 1280)
        self.assertEqual((bound_surface >> 16) & 3, 0)
        self.assertEqual(gpu_info & 0x000F0FFF, bound_info & 0x000F0FFF)
        self.assertNotEqual(gpu_surface & 0x0003FFFF, bound_surface & 0x0003FFFF)
        self.assertNotEqual(0x401 & 0x000F0FFF, bound_info & 0x000F0FFF)

if __name__ == "__main__":
    unittest.main()
