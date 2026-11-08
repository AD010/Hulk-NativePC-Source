#!/usr/bin/env python3
"""Regression: logical Swap frame-end retains Present.r4 front fetch."""
import unittest
from patch_native_x_hulk_present_semantics import OLD, NEW, PRESENT_R4, patch_text

class TestHulkPresentSemantics(unittest.TestCase):
    def test_source_patch_preserves_front_setter(self):
        baseline = "void f() {\n" + PRESENT_R4 + "\n" + OLD + "\n}"
        result, changed = patch_text(baseline)
        self.assertTrue(changed)
        self.assertIn(PRESENT_R4, result)
        self.assertIn("[HULK/NATIVEX] present", result)
        self.assertEqual(patch_text(result), (result, False))
    def test_missing_setter_refused(self):
        with self.assertRaises(ValueError):
            patch_text(OLD)
    def test_stale_split_swap_refused(self):
        with self.assertRaises(ValueError):
            patch_text(PRESENT_R4 + "\n if (call == Call::kSwap) {\n" + OLD)
    def test_unknown_compose_refused(self):
        with self.assertRaises(ValueError):
            patch_text(PRESENT_R4 + "\n changed renderer")
if __name__ == "__main__":
    unittest.main()
