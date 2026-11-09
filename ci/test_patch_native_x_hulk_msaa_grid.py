#!/usr/bin/env python3
"""Pinned Hulk host-only MSAA/Resolve patch guard and expected geometry tests."""
import unittest
from patch_native_x_hulk_msaa_grid import PATCHES, transform

class NativeHulkResolveTests(unittest.TestCase):
    def test_atomic_idempotent_and_drift(self):
        for filename, rules in PATCHES.items():
            with self.subTest(filename=filename):
                raw = "\n".join(old for old, _ in rules)
                revised, changed = transform(raw, rules)
                self.assertTrue(changed)
                self.assertEqual(transform(revised, rules), (revised, False))
                with self.assertRaises(ValueError):
                    transform("upstream changed!", rules)
                partial = rules[0][1] + "\n" + "\n".join(old for old, _ in rules[1:])
                if len(rules) > 1:
                    with self.assertRaises(ValueError):
                        transform(partial, rules)

    def test_preserves_guest_geometry_and_msaa(self):
        cpp = "\n".join(new for rules in PATCHES.values() for _, new in rules)
        self.assertIn("s.msaa() == 2", cpp)
        self.assertIn("s.width == hulk_output_width_ / 2", cpp)
        self.assertIn("scale.x *= 2.0f", cpp)
        self.assertIn("scale.y *= 2.0f", cpp)
        self.assertIn("surface.msaa() == 2", cpp)
        self.assertIn("to.width = std::min(from.width * 2", cpp)
        self.assertIn("bool scaled = source->reduced || aliased", cpp)
        self.assertIn("|| hulk_sample_grid;", cpp)
        self.assertIn("SetHulkSampleGrid", cpp)
        self.assertNotIn("surface.width *= 2", cpp)
        self.assertNotIn("surface.surface_info &=", cpp)
        self.assertIn("if (!has_surface)", cpp)

if __name__ == "__main__":
    unittest.main()
