#!/usr/bin/env python3
"""Smoke tests for bounded Hulk output-scissor and live MSAA resolve audit."""
import unittest
from patch_native_x_hulk_output_scissor import PATCHES, transform

class HulkOutputScissorTests(unittest.TestCase):
    def test_pinned_patch_idempotence_and_fail_closed(self):
        for path, rules in PATCHES.items():
            with self.subTest(path=path):
                original = "\n".join(old for old, new in rules)
                updated, changed = transform(original, rules)
                self.assertTrue(changed)
                self.assertEqual(transform(updated, rules), (updated, False))
                with self.assertRaises(ValueError):
                    transform("unrelated source", rules)
                if len(rules) > 1:
                    partially_patched = rules[0][1] + "\n" + "\n".join(old for old, new in rules[1:])
                    with self.assertRaises(ValueError):
                        transform(partially_patched, rules)

    def test_exact_half_width_output_only(self):
        patch = "\n".join(new for old, new in PATCHES["src/draw.cpp"])
        self.assertIn("native_x_renderer_hulk_full_output_scissor, false", patch)
        self.assertIn("sized->guest_width == 1280", patch)
        self.assertIn("sized->guest_height == 720", patch)
        self.assertIn("scissor.extent[0] == 640", patch)
        self.assertIn("viewport_info.xy_extent[0] >= 1280", patch)
        self.assertIn("!sized->depth", patch)
        self.assertIn("scissor_rect.right = int32_t(sized->width)", patch)
        self.assertIn("[HULK/SCISSOR-FIX]", patch)
        self.assertNotIn("viewport.Width =", patch)

    def test_full_msaa_scene_resolve_audit(self):
        patch = "\n".join(new for old, new in PATCHES["src/renderer.cpp"])
        self.assertIn("surface.msaa() == 2", patch)
        self.assertIn("surface.width == 640", patch)
        self.assertIn("surface.height == 360", patch)
        self.assertIn("target ? target->guest_width : 0", patch)
        self.assertIn("[HULK/SCENE-RESOLVE]", patch)

if __name__ == "__main__":
    unittest.main()
