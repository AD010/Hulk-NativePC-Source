#!/usr/bin/env python3
"""Native Vulkan Hulk: verify host-scale Resolve and LR trace cannot alter guest."""
import unittest
from patch_native_x_hulk_resolve_scale import PATCHES, transform

class HulkResolveScaleTests(unittest.TestCase):
    def test_idempotent_and_fail_closed(self):
        for path, rules in PATCHES.items():
            with self.subTest(path=path):
                old = "\n".join(b for b, a in rules)
                after, changed = transform(old, rules)
                self.assertTrue(changed)
                self.assertEqual(transform(after, rules), (after, False))
                with self.assertRaises(ValueError):
                    transform("unknown", rules)

    def test_real_source_target_scale_is_checked(self):
        patched = "\n".join(a for b,a in PATCHES["src/renderer.cpp"])
        self.assertIn("source->scale.x != target->scale.x", patched)
        self.assertIn("hulk_host_scale_mismatch", patched)
        self.assertIn("hulk_sample_grid || hulk_host_scale_mismatch;", patched)
        self.assertIn("[HULK/RESOLVE-CALLER]", patched)
        self.assertIn("[HULK/RESOLVE-SCALE]", patched)
        self.assertIn("scene4x ? \"scene4x\" : \"output\"", patched)
        self.assertNotIn("surface.width *= 2", patched)
        self.assertNotIn("surface.surface_info &=", patched)

if __name__ == "__main__":
    unittest.main()
