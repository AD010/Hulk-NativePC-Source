#!/usr/bin/env python3
"""Fail-closed static tests for Hulk's ordered Vulkan PM4 gap draw replay."""
import unittest
from patch_native_x_hulk_gap_stream import PATCHES, transform
from patch_native_x_hulk_pm4_gap import EMPTY_LEAVE_ANCHOR, EMPTY_LEAVE_NEW

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
        # No GPU behavior change: only reduce excessive INFO lines in log203.
        self.assertIn("HULK/GAP-STREAM-LOG-BUDGET-203", cpp)
        self.assertIn("before / 10000 != (before + emitted) / 10000", cpp)
        self.assertIn("++emitted;", cpp)
        self.assertIn("if (single_direct_draw && keep_record_", cpp)


    def test_empty_setter_does_not_replay_an_already_consumed_gap(self):
        # Regression: D140 #108 skipped the Hulk Enter gap scan in lean
        # mode; once lean is OFF, empty setters must still consume the gap.
        self.assertIn("return nullptr;", EMPTY_LEAVE_ANCHOR)
        self.assertIn("HULK/PM4-GAP-CONSUME-EMPTY", EMPTY_LEAVE_NEW)
        self.assertIn("last_hulk_segment_end_ = p.begin;", EMPTY_LEAVE_NEW)
        self.assertIn("last_hulk_segment_valid_ = false;", EMPTY_LEAVE_NEW)
        self.assertIn("last_hulk_segment_device_ == p.device", EMPTY_LEAVE_NEW)
        self.assertIn("p.begin - last_hulk_segment_end_ <= 0x40000u", EMPTY_LEAVE_NEW)
        self.assertNotIn("CaptureDraw", EMPTY_LEAVE_NEW)
        self.assertNotIn("ScanPackets", EMPTY_LEAVE_NEW)
        self.assertNotIn("Gather(", EMPTY_LEAVE_NEW)

        def consume(cursor, begin, *, valid=True, same_device=True):
            if valid and same_device and begin >= cursor:
                if begin - cursor <= 0x40000:
                    return begin, True
                return cursor, False
            return cursor, valid
        # A PM4 draw gap [100, 140) is scanned by Enter, then the empty
        # setter consumes it. Later setters see no new GPU bytes.
        cursor, valid = consume(100, 140)
        self.assertEqual((cursor, valid), (140, True))
        for begin in (140, 140, 140):
            self.assertEqual(consume(cursor, begin), (140, True))
        self.assertEqual(consume(cursor, 180), (180, True))
        # Never fold a new device or a backwards/large ring discontinuity
        # into a previous device's PM4 history.
        self.assertEqual(consume(140, 180, same_device=False), (140, True))
        self.assertEqual(consume(140, 100), (140, True))
        self.assertEqual(consume(140, 140 + 0x40004), (140, False))

    def test_native_capture_uses_existing_draw_path(self):
        files = "\n".join(new for rules in PATCHES.values() for old, new in rules)
        self.assertIn("renderer::OnHulkGapSingleDraw(tracker, device, base)", files)
        self.assertNotIn("Xenos", PATCHES["src/native_x_renderer.cpp"][0][1])

if __name__ == "__main__":
    unittest.main()
