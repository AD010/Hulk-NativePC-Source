#!/usr/bin/env python3
"""Synthetic PM4 parser failure provenance; no XEX/arc/shader data required."""
import unittest
from patch_native_x_hulk_pm4_first_failure import (
    patch, HEADER_OLD, SCAN_HEAD, CLEAR_OLD, LOG_OLD,
    LOG_ARGS_OLD, HEADER_NEW, SCAN_HEAD_NEW, LOG_NEW,
)


def fixture() -> dict[str, str]:
    return {
        "src/state.h": "struct PacketScan {\n" + HEADER_OLD + "\n};",
        "src/state.cpp": (
            "void ScanPackets() {\n" + SCAN_HEAD + "\n"
            "  if (p > end) return void(out.complete = false);\n"
            "  if (p == end) out.complete = false;\n"
            "  if (header == 1) out.complete = false;\n"
            "  if (header == 2) out.complete = false;\n"
            "  if (header == 3) out.complete = false;\n"
            "}\n" + "bool FetchSlotDirty() {}\n"
            "void PacketScan::Clear() {\n" + CLEAR_OLD + "\n}\n"
            "// HULK/INCOMPLETE-XDK-DRAW-GUARD\n"
            "REXLOG_WARN(\n" + LOG_OLD + "\n"
            + LOG_ARGS_OLD + "\n"
        ),
    }


class TestHulkPm4FirstFailure(unittest.TestCase):
    def test_complete_patch_and_idempotence(self):
        fixed, changed = patch(fixture())
        self.assertTrue(changed)
        self.assertEqual(patch(fixed), (fixed, False))
        self.assertEqual(fixed["src/state.cpp"].count("hulk_note_failure(__LINE__)"), 5)
        self.assertIn(SCAN_HEAD_NEW, fixed["src/state.cpp"])
        self.assertIn(HEADER_NEW, fixed["src/state.h"])
        for token in ("fail_site={}", "fail_header={:#x}", "fail_depth={}",
                      "fail_remaining={}", "hulk_fail_header >> 30",
                      "hulk_fail_remaining = p <= end ? end - p : 0u;"):
            self.assertIn(token, fixed["src/state.cpp"])
        self.assertIn("return void(hulk_note_failure(__LINE__))", fixed["src/state.cpp"])
        self.assertNotIn("HULK/PM4-FIRST-FAILURE", fixture()["src/state.cpp"])

    def test_partial_and_unreviewed_sources_rejected(self):
        input_files = fixture()
        input_files["src/state.h"] = input_files["src/state.h"].replace(
            HEADER_OLD, HEADER_NEW)
        with self.assertRaises(ValueError):
            patch(input_files)
        input_files = fixture()
        input_files["src/state.cpp"] = input_files["src/state.cpp"].replace(
            "void ScanPackets()", "void UnknownFunction()")
        with self.assertRaises(ValueError):
            patch(input_files)
        input_files = fixture()
        input_files["src/state.cpp"] = input_files["src/state.cpp"].replace(
            "  if (header == 3) out.complete = false;\n", ""
        input_files["src/state.cpp"] = input_files["src/state.cpp"].replace(
            "  if (header == 2) out.complete = false;\n", "")
        with self.assertRaises(ValueError):
            patch(input_files)  # fewer than three verified fail sites

    def test_no_raster_or_descriptor_mutations(self):
        data, _ = patch(fixture())
        scanner = data["src/state.cpp"]
        self.assertNotIn("gpu_allow_invalid_fetch_constants", scanner)
        self.assertNotIn("c.programs[1] =", scanner)
        self.assertNotIn("SetWaitForPipelines(", scanner)
        self.assertNotIn("DrawInternal(", scanner)
        self.assertIn("if (p > end) return void(hulk_note_failure(__LINE__))", scanner)


if __name__ == "__main__":
    unittest.main()
