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
        self.assertIn("[HULK/FETCH-ORIGIN]",src)
        self.assertIn("[HULK/FETCH-WRITER]",src)
        self.assertIn("[HULK/SHADER-LIFETIME]", src)
        self.assertIn("const bool copied = load.ucode_offset != PacketScan::kInGuestMemory", src)
        self.assertIn("const uint64_t captured_hash = XXH3_64bits(ucode, size)", src)
        self.assertIn("const uint64_t live_hash = XXH3_64bits(live, size)", src)
        self.assertIn("captured_hash != live_hash", src)

        self.assertIn("(reg - kFetchConstants) / 6",src)
        self.assertIn("binding.fetch_constant",src)
        self.assertIn("[HULK/DMA-ORIGIN]",src)
        self.assertIn("hulk_d.num_indices > hulk_s.num_words",src)
        self.assertNotIn("native_x_renderer_resolution_scale = 1",src)

if __name__ == "__main__":
    unittest.main()
