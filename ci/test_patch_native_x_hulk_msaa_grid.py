#!/usr/bin/env python3
"""Fail-closed tests for opt-in Hulk native MSAA sample-grid conversion."""
import unittest
from patch_native_x_hulk_msaa_grid import PATCHES, transform

class HulkMsaaGridTests(unittest.TestCase):
    def test_rules_exact_and_reentrant(self):
        for filename, rules in PATCHES.items():
            with self.subTest(filename=filename):
                source = "\n".join(old for old, new in rules)
                after, changed = transform(source, rules)
                self.assertTrue(changed)
                self.assertEqual(transform(after, rules), (after, False))
                with self.assertRaises(ValueError):
                    transform("unrelated upstream", rules)
                mixed = rules[0][1] + "\n" + "\n".join(old for old, new in rules[1:])
                with self.assertRaises(ValueError):
                    transform(mixed, rules)

    def test_only_hulk_half_size_four_sample_buffers(self):
        replacement = "\n".join(new for old, new in PATCHES["src/renderer.cpp"])
        self.assertIn("surface.msaa() == 2", replacement)
        self.assertIn("surface.width == g_profile->output_width / 2", replacement)
        self.assertIn("surface.height == g_profile->output_height / 2", replacement)
        self.assertIn("surface.surface_info &= ~(3u << 16)", replacement)
        self.assertIn("surface.depth ? \"depth\" : \"color\"", replacement)
        self.assertIn("has_surface", replacement)
        self.assertIn("[HULK/MSAA-GRID]", replacement)
        self.assertIn("[HULK/PM4-GUARD]", replacement)
        self.assertIn("native_x_renderer_hulk_expand_msaa_sample_grid, false", replacement)

if __name__ == "__main__":
    unittest.main()
