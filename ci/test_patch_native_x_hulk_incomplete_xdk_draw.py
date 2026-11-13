#!/usr/bin/env python3
"""Regression: incomplete XDK PM4 may never reach Gather or CaptureDraw."""
import unittest
from patch_native_x_hulk_incomplete_xdk_draw import (patch, patch_enter, patch_header,
    ANCHOR, PATCH, ENTER_OLD, ENTER_NEW, PENDING_HEADER_OLD, PENDING_HEADER_NEW)

class IncompleteXdkTests(unittest.TestCase):
    def test_atomic_idempotent(self):
        source = "const TrackedCall* StateTracker::Leave(Call call, const uint8_t* base, bool nested) {\n" + ANCHOR
        out, changed = patch(source)
        self.assertTrue(changed)
        self.assertEqual(patch(out), (out, False))
        self.assertIn("if (!scan_.complete)", out)
        self.assertIn("if (!scan_.complete && call == Call::kPresent)", out)
        self.assertIn("[HULK/PRESENT-FRAME-RECOVERY]", out)
        self.assertIn("return &last_;", out)
        self.assertLess(out.index("if (!scan_.complete && call == Call::kPresent)"),
                        out.index("if (!scan_.complete)"))
        self.assertLess(out.index("scan_.Clear();\n    last_.call = call;"),
                        out.index("if (!scan_.complete)"))
        self.assertIn("last_.scan = &scan_;\n    return &last_;", out)
        self.assertIn("last_hulk_segment_valid_ = false;", out)
        self.assertIn("inline_begin_.pending = false;", out)
        self.assertIn("inline_begin_.end = 0;", out)
        self.assertLess(out.index("inline_begin_.pending = false;"), out.rindex("scan_.Clear();"))
        self.assertIn("scan_.Clear();\n    return nullptr;", out)
        self.assertLess(out.index("if (!scan_.complete)"), out.index(ANCHOR))
        self.assertIn("HULK/INCOMPLETE-XDK-DRAW-GUARD", out)
        self.assertIn("std::atomic<uint64_t>", out)
        self.assertIn("HULK/LOG192-PM4-BOUNDS", out)
        self.assertIn("end >= p.scan_from", out)
        self.assertIn("p.begin, p.scan_from, end, p.kick", out)
        self.assertIn("packet_draws={} indirect={}", out)
        # The original Present callback still runs; guard remains non-invasive.
        self.assertLess(out.index("if (!scan_.complete && call == Call::kPresent)"),
                        out.index("if (!scan_.complete)"))


    def test_rejected_draw_and_present_rollback_all_bin_state(self):
        cpp = ("void StateTracker::Enter(Call call, uint32_t device, const uint8_t* base, bool nested) {\n"
               + ENTER_OLD
               + "\n}\nconst TrackedCall* StateTracker::Leave(Call call, const uint8_t* base, bool nested) {\n"
               + ANCHOR)
        cpp, changed = patch_enter(cpp)
        self.assertTrue(changed)
        self.assertEqual(patch_enter(cpp), (cpp, False))
        cpp, changed = patch(cpp)
        self.assertTrue(changed)
        self.assertEqual(patch(cpp), (cpp, False))
        self.assertIn(ENTER_NEW, cpp)
        self.assertEqual(cpp.count("scan_.bin_mask = p.hulk_bin_mask_at_enter;"), 2)
        self.assertEqual(cpp.count("scan_.bin_select = p.hulk_bin_select_at_enter;"), 2)
        # Both incomplete branches must restore the outer Enter checkpoint
        # before Clear(), which intentionally retains BIN state.
        present = cpp.split("if (!scan_.complete && call == Call::kPresent)", 1)[1].split("if (!scan_.complete)", 1)[0]
        rejected = cpp.split("if (!scan_.complete) {", 1)[1].split("return nullptr;", 1)[0]
        for branch in (present, rejected):
            for field in ("mask", "select"):
                restore = f"scan_.bin_{field} = p.hulk_bin_{field}_at_enter;"
                self.assertEqual(branch.count(restore), 1)
                self.assertLess(branch.index(restore), branch.index("scan_.Clear();"))
        self.assertLess(cpp.index("p.hulk_bin_mask_at_enter = scan_.bin_mask;"),
                        cpp.index("if (!scan_.complete && call == Call::kPresent)"))
        header, h_changed = patch_header(PENDING_HEADER_OLD)
        self.assertTrue(h_changed)
        self.assertEqual(patch_header(header), (header, False))
        self.assertIn(PENDING_HEADER_NEW, header)
        with self.assertRaises(ValueError):
            patch_enter(cpp.replace(ENTER_NEW, "  // missing XDK Enter checkpoint"))

    def test_source_drift_is_rejected(self):
        source = "const TrackedCall* StateTracker::Leave(Call call, const uint8_t* base, bool nested) {\n" + ANCHOR
        with self.assertRaises(ValueError):
            patch(source.replace(ANCHOR, "Gather(p, call, draw, base);"))
        with self.assertRaises(ValueError):
            patch(source + ANCHOR)
        with self.assertRaises(ValueError):
            patch("HULK/INCOMPLETE-XDK-DRAW-GUARD\n" + source)

if __name__ == "__main__":
    unittest.main()
