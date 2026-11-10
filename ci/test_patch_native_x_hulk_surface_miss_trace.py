#!/usr/bin/env python3
"""Regression: PM4 miss diagnostics neither accept nor synthesize an attachment."""
import unittest
from patch_native_x_hulk_surface_miss_trace import OLD, NEW, ACCEPT_OLD, ACCEPT_NEW, patch_text

class TestMissTrace(unittest.TestCase):
    def test_unique_and_idempotent(self):
        original = "bool HulkPacketSurfaces(" + "\n" + OLD + "\n" + ACCEPT_OLD
        result, changed = patch_text(original)
        self.assertTrue(changed)
        self.assertEqual(patch_text(result), (result, False))
        self.assertIn("HULK/PM4-MISS", result)
        self.assertIn("HULK/PM4-ACCEPT", result)
        self.assertIn("hulk_packet_draws_mapped", result)
        for s in ("void unrelated() {}", original + "\n" + OLD):
            with self.assertRaises(ValueError): patch_text(s)
    def test_does_not_change_reject_path(self):
        self.assertEqual(OLD.count("unresolved = true"), NEW.count("unresolved = true"))
        self.assertEqual(OLD.count("if (!has_target || unresolved)"), NEW.count("if (!has_target || unresolved)"))
        self.assertNotIn("out[slot] =", NEW)
        self.assertIn("bound ? bound.info", NEW)
        self.assertIn("misses % 20000", NEW)
        self.assertIn("mapped % 50000", ACCEPT_NEW)
        self.assertNotIn("out[slot] =", ACCEPT_NEW)

if __name__ == "__main__": unittest.main()
