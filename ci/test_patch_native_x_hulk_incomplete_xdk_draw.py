#!/usr/bin/env python3
"""Regression: incomplete XDK PM4 may never reach Gather or CaptureDraw."""
import unittest
from patch_native_x_hulk_incomplete_xdk_draw import patch, ANCHOR, PATCH

class IncompleteXdkTests(unittest.TestCase):
    def test_atomic_idempotent(self):
        source = "const TrackedCall* StateTracker::Leave(Call call, const uint8_t* base, bool nested) {\n" + ANCHOR
        out, changed = patch(source)
        self.assertTrue(changed)
        self.assertEqual(patch(out), (out, False))
        self.assertIn("if (!scan_.complete)", out)
        self.assertIn("last_hulk_segment_valid_ = false;", out)
        self.assertIn("inline_begin_.pending = false;", out)
        self.assertIn("inline_begin_.end = 0;", out)
        self.assertLess(out.index("inline_begin_.pending = false;"), out.index("scan_.Clear();"))
        self.assertIn("scan_.Clear();\n    return nullptr;", out)
        self.assertLess(out.index("if (!scan_.complete)"), out.index(ANCHOR))
        self.assertIn("HULK/INCOMPLETE-XDK-DRAW-GUARD", out)
        self.assertIn("std::atomic<uint64_t>", out)

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
