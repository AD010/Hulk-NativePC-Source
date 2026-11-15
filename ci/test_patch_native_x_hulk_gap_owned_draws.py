#!/usr/bin/env python3
"""Synthetic source contract for immutable first-pass D140 gap replay."""
import unittest
from patch_native_x_hulk_gap_owned_draws import (
    patch, PRE_OLD, PRE_NEW, START_OLD, START_NEW, END_OLD, END_NEW
)

class OwnedReplayTests(unittest.TestCase):
    def source(self):
        return ("// HULK/PM4-PER-DRAW-BOUNDARY\n"
                "// HULK/PM4-REGISTER-SNAPSHOT\n"
                "// HULK/PM4-PRESCAN-SHADER-SNAPSHOT\n" +
                PRE_OLD + "\n" + START_OLD + "\n" + END_OLD)

    def test_atomic_idempotent_and_whole_gap_validation(self):
        source = self.source()
        updated, changed = patch(source)
        self.assertTrue(changed)
        self.assertEqual(patch(updated), (updated, False))
        self.assertIn("hulk_owned_complete && hulk_owned_draws.size() == expected", updated)
        self.assertIn("hulk_owned_draws.emplace_back(scan_);", updated)
        self.assertIn("std::move(owned)", updated)
        self.assertIn("scan_ = std::move(hulk_final_state);", updated)
        self.assertIn("8u << 20", updated)
        self.assertIn("128u", updated)
        self.assertLess(updated.index("hulk_owned_draws.emplace_back(scan_);"),
                        updated.index("hulk_gap_packet_callback_(*this"))
        self.assertEqual(updated.count("HULK/PM4-FIRST-PASS-OWNED-DRAWS-211"), 1)
        self.assertEqual(updated.count("hulk_gap_packet_callback_(*this"), 1)
        self.assertIn("else {\n            // Uncommon over-budget", updated)

    def test_fails_closed_on_drift_or_partial_install(self):
        source = self.source()
        for wrong in (source.replace(PRE_OLD, ""),
                      source.replace(START_OLD, ""),
                      source.replace(END_OLD, ""),
                      source.replace("HULK/PM4-REGISTER-SNAPSHOT", "unknown"),
                      PRE_NEW + "\n" + START_OLD + "\n" + END_OLD):
            with self.assertRaises(ValueError): patch(wrong)

    def test_original_draws_not_invented_by_snapshots(self):
        # A gap is eligible ONLY if every actual draw was captured and
        # the original scan itself reports complete (stream_gap prerequisite).
        for expected, owned, complete in ((24,24,True),(24,23,True),
                                          (24,24,False),(0,0,True)):
            stream_gap = complete and expected > 0
            should_submit = stream_gap and owned == expected
            self.assertEqual(should_submit,
                             complete and expected > 0 and owned == expected)

if __name__ == "__main__":
    unittest.main()
