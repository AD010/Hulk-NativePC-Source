#!/usr/bin/env python3
"""Fail-closed regression: one source patch with nested Swap/front + outer Present/frame."""
import unittest
from patch_native_x_hulk_present_semantics import RULES, patch_text

class TestHulkPresentSemantics(unittest.TestCase):
    def test_source_patch_and_idempotence(self):
        baseline = "\n// separated\n".join(old for old, _ in RULES)
        result, changed = patch_text(baseline)
        self.assertTrue(changed)
        for _, new in RULES:
            self.assertEqual(result.count(new), 1)
        self.assertFalse(patch_text(result)[1])
        self.assertEqual(result, patch_text(result)[0])
    def test_mismatch_refused(self):
        baseline = "\n// separated\n".join(old for old, _ in RULES)
        with self.assertRaises(ValueError):
            patch_text(baseline.replace(RULES[0][0], "incorrect upstream"),)
        with self.assertRaises(ValueError):
            patch_text(baseline.replace(RULES[0][0], RULES[0][1]))

if __name__ == "__main__":
    unittest.main()
