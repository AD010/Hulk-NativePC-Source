#!/usr/bin/env python3
"""Regression checks for native Vulkan pre-HUD AA and PM4 topology guard."""
import unittest
from patch_native_x_hulk_scene_aa import PATCHES as AA, transform as aa_transform
from patch_native_x_hulk_geometry_guard import PATCHES as GEOM, transform as geom_transform

class HulkNativeAAAndGeometryGuard(unittest.TestCase):
    def test_exact_patch_and_idempotence(self):
        for patch, transform in [(AA, aa_transform), (GEOM, geom_transform)]:
            for path, rules in patch.items():
                with self.subTest(path=path):
                    source = "\n".join(old for old, new in rules)
                    output, changed = transform(source, rules)
                    self.assertTrue(changed)
                    self.assertEqual(transform(output, rules), (output, False))
                    with self.assertRaises(ValueError):
                        transform("unknown upstream", rules)

    def test_no_fullscreen_after_hud_smoothing(self):
        cpp = "\n".join(new for old, new in AA["src/renderer.cpp"])
        self.assertIn("native_x_renderer_hulk_auto_scene_aa, false", cpp)
        self.assertIn("surface.msaa() == 2", cpp)
        self.assertIn("target->guest_width == g_profile->output_width", cpp)
        self.assertIn("!g_scene_antialiased", cpp)
        self.assertIn("[HULK/SCENE-AA]", cpp)
        self.assertNotIn("CapturePresent", cpp)
        self.assertNotIn("ComposePicture", cpp)

    def test_reject_synthetic_corrupted_topology_only(self):
        cpp = "\n".join(new for old, new in GEOM["src/renderer.cpp"])
        self.assertIn("LastWrite(rg::XE_GPU_REG_VGT_DRAW_INITIATOR", cpp)
        self.assertIn("topology == 0 || topology == 48 || topology == 49", cpp)
        self.assertIn("[HULK/GEOMETRY-GUARD]", cpp)
        self.assertNotIn("CaptureDraw", cpp)

if __name__ == "__main__":
    unittest.main()
