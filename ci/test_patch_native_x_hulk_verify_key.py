#!/usr/bin/env python3
import unittest
from patch_native_x_hulk_verify_key import (
    OLD_BIND, OLD_LOG, NEW_BIND, NEW_LOG, patch_text)

class TestVerifyHotkey(unittest.TestCase):
    def test_one_time_and_idempotence(self):
        original = OLD_BIND + "\n" + OLD_LOG
        patched, change = patch_text(original)
        self.assertTrue(change)
        self.assertIn(NEW_BIND, patched)
        self.assertIn(NEW_LOG, patched)
        self.assertNotIn(OLD_BIND, patched)
        self.assertEqual(patch_text(patched), (patched, False))
    def test_mixed_must_refuse(self):
        with self.assertRaises(ValueError):
            patch_text(NEW_BIND + "\n" + OLD_LOG)

if __name__ == "__main__":
    unittest.main()
