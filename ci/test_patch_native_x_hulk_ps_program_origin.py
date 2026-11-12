#!/usr/bin/env python3
"""Synthetic regression for Hulk frame2157 PS provenance (no game bytes)."""
import unittest
from patch_native_x_hulk_ps_program_origin import patch, ANCHOR, CXX


class ProgramOriginTest(unittest.TestCase):
    def test_atomic_idempotent_no_shader_substitution(self):
        upstream = "#include <algorithm>\n" + ANCHOR + "\n"
        result, changed = patch(upstream)
        self.assertTrue(changed)
        self.assertEqual(patch(result), (result, False))
        self.assertEqual(result.count(CXX), 1)
        self.assertIn("#include <atomic>", result)
        for needle in ("0xe42552fce84cb8acull", "0x0dad3cb9972d38fdull",
                       "hulk_ps->loaded_by", "hulk_ps->packet",
                       "hulk_ps->address", "hulk_ps->immediate",
                       "g_frames_presented", "draw_capture={}"):
            self.assertIn(needle, result)
        self.assertNotIn("c.programs[1] =", CXX)
        self.assertNotIn("return;", CXX)
        self.assertNotIn("CreateShader", CXX)
        self.assertLess(result.index(ANCHOR), result.index("HULK/PS-PROGRAM-ORIGIN"))
        with self.assertRaises(ValueError):
            patch("unreviewed upstream")
        with self.assertRaises(ValueError):
            patch(ANCHOR + "\n" + ANCHOR)
        with self.assertRaises(ValueError):
            patch("HULK/PS-PROGRAM-ORIGIN\n" + upstream)

    def test_bounded_logging_and_consecutive_mismatch(self):
        # The old verifier supplied only the first 24 examples of 49
        # mismatches. Do not incorrectly claim all 49 were consecutive.
        observed = list(range(1819, 1843))
        self.assertEqual(len(observed), 24)
        self.assertEqual(observed[-1] - observed[0] + 1, len(observed))
        self.assertIn("hulk_change_no <= 128", CXX)
        self.assertIn("hulk_capture_no % 2400 == 0", CXX)
        self.assertIn("std::memory_order_relaxed", CXX)


if __name__ == "__main__":
    unittest.main()
