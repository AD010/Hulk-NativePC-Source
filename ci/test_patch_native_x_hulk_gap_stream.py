#!/usr/bin/env python3
"""Fail-closed static tests for Hulk's ordered Vulkan PM4 gap draw replay."""
import unittest
from patch_native_x_hulk_gap_stream import PATCHES, transform

class HulkStreamPatchTests(unittest.TestCase):
    def test_pinned_anchor_transform_is_atomic_and_idempotent(self):
        for filename, rules in PATCHES.items():
            with self.subTest(filename=filename):
                before = "\n".join(old for old, new in rules)
                after, changed = transform(before, rules)
                self.assertTrue(changed)
                self.assertEqual(transform(after, rules), (after, False))
                self.assertRaises(ValueError, transform, "anchor drift", rules)
                if len(rules) > 1:
                    partial = rules[0][1] + "\n" + "\n".join(old for old, new in rules[1:])
                    self.assertRaises(ValueError, transform, partial, rules)

    def test_ordered_draws_not_last_state_snapshots(self):
        cpp = "\n".join(new for old, new in PATCHES["src/state.cpp"])
        self.assertIn("on_packet_draw", cpp)
        self.assertIn("if (on_packet_draw) on_packet_draw()", cpp)
        self.assertIn("skip_constants, copy_microcode, on_packet_draw", cpp)
        self.assertIn("Gather(p, call, false, base);", cpp)
        self.assertIn("hulk_gap_packet_callback_(*this, p.device, base);", cpp)
        self.assertIn("scan_.Clear();", cpp)
        self.assertIn("scan_.draws <= 4096", cpp)
        self.assertIn("scan_.indirect <= 256", cpp)
        self.assertIn("if (!scan_.complete || emitted != expected)", cpp)
        self.assertIn("const bool stream_gap = scan_.complete &&", cpp)
        self.assertIn("const bool single_direct_draw = scan_.complete &&", cpp)
        self.assertIn("if (scan_.complete) {", cpp)
        self.assertIn("rejected incomplete PM4 state before Draw/Gather", cpp)
        self.assertIn("if (single_direct_draw && keep_record_", cpp)


    def test_native_capture_uses_existing_draw_path(self):
        files = "\n".join(new for rules in PATCHES.values() for old, new in rules)
        self.assertIn("renderer::OnHulkGapSingleDraw(tracker, device, base)", files)
        self.assertNotIn("Xenos", PATCHES["src/native_x_renderer.cpp"][0][1])

if __name__ == "__main__":
    unittest.main()
