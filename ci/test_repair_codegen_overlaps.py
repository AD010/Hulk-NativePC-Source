"""Regression tests for the 2026-11-05 ReXGlue overlap failures."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from repair_codegen_overlaps import repair


class CodegenBoundaryTests(unittest.TestCase):
    def test_data_pointer_collision_from_run_7(self):
        log = "[config] Overlapping boundaries: 0x8283D8B8+0x64 overlaps 0x8283D8D0+0x0\n"
        missing = "[functions]\n0x8283D8D0 = {}\n"
        indirect = "[functions]\n0x8283D8B8 = { end = 0x8283D91C }  # data ptr @0x820894D8\n"
        result, changes = repair(log, missing, indirect)
        self.assertIn("end = 0x8283D8D0", result)
        self.assertEqual(changes, {0x8283D8B8: 0x8283D8D0})

    def test_original_pointer_scan_collision(self):
        log = "[config] Overlapping boundaries: 0x8212C2A0+0x30 overlaps 0x8212C2C0+0x0\n"
        missing = "[functions]\n0x8212C2C0 = {}\n"
        indirect = "[functions]\n0x8212C2A0 = { end = 0x8212C2D0 }  # pointer scan\n"
        result, changes = repair(log, missing, indirect)
        self.assertIn("end = 0x8212C2C0", result)
        self.assertEqual(changes, {0x8212C2A0: 0x8212C2C0})

    def test_no_repeated_split(self):
        log = "[config] Overlapping boundaries: 0x8283D8B8+0x64 overlaps 0x8283D8D0+0x0\n"
        missing = "[functions]\n0x8283D8D0 = {}\n"
        already_cut = "[functions]\n0x8283D8B8 = { end = 0x8283D8D0 }  # data ptr @0x820894D8\n"
        with self.assertRaisesRegex(ValueError, "not an identical inferred range"):
            repair(log, missing, already_cut)

    def test_never_modify_unknown_manual_range(self):
        log = "[config] Overlapping boundaries: 0x8283D8B8+0x64 overlaps 0x8283D8D0+0x0\n"
        missing = "[functions]\n0x8283D8D0 = {}\n"
        manual = "[functions]\n0x8283D8B8 = { end = 0x8283D91C }  # manually verified\n"
        with self.assertRaisesRegex(ValueError, "not an identical inferred range"):
            repair(log, missing, manual)

    def test_multiple_inner_starts_split_at_first(self):
        log = ("[config] Overlapping boundaries: 0x8212C2A0+0x30 overlaps 0x8212C2C0+0x0\n"
               "[config] Overlapping boundaries: 0x8212C2A0+0x30 overlaps 0x8212C2B0+0x0\n")
        missing = "[functions]\n0x8212C2B0 = {}\n0x8212C2C0 = {}\n"
        indirect = "[functions]\n0x8212C2A0 = { end = 0x8212C2D0 }  # pointer scan\n"
        result, changes = repair(log, missing, indirect)
        self.assertIn("end = 0x8212C2B0", result)
        self.assertEqual(changes, {0x8212C2A0: 0x8212C2B0})


if __name__ == "__main__":
    unittest.main()
