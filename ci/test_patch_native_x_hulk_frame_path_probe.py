#!/usr/bin/env python3
"""Check pinned Hulk Vulkan frame geometry probe's anchors and output tags."""
import unittest
from patch_native_x_hulk_frame_path_probe import PATCHES, transform

class FramePathProbeTests(unittest.TestCase):
    def test_atomic_and_idempotent(self):
        for rel, rules in PATCHES.items():
            with self.subTest(rel=rel):
                raw = "\n".join(old for old,new in rules)
                after, changed = transform(raw, rules)
                self.assertTrue(changed)
                self.assertEqual(transform(after,rules),(after,False))
                with self.assertRaises(ValueError):
                    transform("anchor mismatch",rules)
    def test_all_three_stages_logged_without_scaling_shortcut(self):
        src = "\n".join(new for rules in PATCHES.values() for old,new in rules)
        self.assertIn("draw#{}",src)
        self.assertIn("resolve guest",src)
        self.assertIn("resolve host",src)
        self.assertIn("present source",src)
        self.assertNotIn("native_x_renderer_resolution_scale = 1",src)

if __name__ == "__main__":
    unittest.main()
