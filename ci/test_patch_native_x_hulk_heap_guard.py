#!/usr/bin/env python3
"""Static contract tests: native mapped data must never be blindly copied after failure."""
import unittest
from patch_native_x_hulk_heap_guard import DRAW, OVERLAY, patch_text

class TestNativeXHeapGuard(unittest.TestCase):
    def test_complete_pinned_anchor_patch_is_idempotent(self):
        for rules in (DRAW, OVERLAY):
            sample = "\n// separate\n".join(old for old, _ in rules)
            changed, applied = patch_text(sample, rules)
            self.assertTrue(applied)
            for old, new in rules:
                self.assertEqual(changed.count(new), 1)
            unchanged, applied_again = patch_text(changed, rules)
            self.assertFalse(applied_again)
            self.assertEqual(changed, unchanged)

    def test_partial_or_changed_anchor_refused(self):
        for rules in (DRAW, OVERLAY):
            sample = "\n// separate\n".join(old for old, _ in rules)
            with self.assertRaises(ValueError):
                patch_text(sample.replace(rules[0][0], rules[0][1]), rules)
            with self.assertRaises(ValueError):
                patch_text(sample.replace(rules[0][0], "WRONG ABI"), rules)

if __name__ == "__main__":
    unittest.main()
