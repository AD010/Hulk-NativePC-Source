#!/usr/bin/env python3
"""Synthetic PM4 parser failure provenance; no XEX/arc/shader data required."""
import unittest
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from patch_native_x_hulk_pm4_first_failure import (
    patch, HEADER_OLD, SCAN_HEAD, CLEAR_OLD, LOG_OLD,
    LOG_ARGS_OLD, HEADER_NEW, SCAN_HEAD_NEW, SCAN_TAIL_NEW, LOG_NEW,
)


def fixture() -> dict[str, str]:
    return {
        "src/state.h": "struct PacketScan {\n" + HEADER_OLD + "\n};",
        "src/state.cpp": (
            "void ScanPackets() {\n" + SCAN_HEAD + "\n"
            "    switch (header >> 30) {\n"
            "  if (p > end) return void(out.complete = false);\n"
            "  if (p == end) out.complete = false;\n"
            "  if (header == 1) out.complete = false;\n"
            "  if (header == 2) out.complete = false;\n"
            "  if (header == 3) out.complete = false;\n"
            "    }\n  }\n}\n\n" + "bool FetchSlotDirty() {}\n"
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
        self.assertIn(SCAN_TAIL_NEW, fixed["src/state.cpp"])
        self.assertIn(HEADER_NEW, fixed["src/state.h"])
        for token in ("fail_site={}", "fail_header={:#x}", "fail_depth={}",
                      "fail_remaining={}", "hulk_fail_header >> 30",
                      "hulk_fail_remaining = p <= end ? end - p : 0u;",
                      "last_good_begin={:#x}", "hulk_last_good_header"):
            self.assertIn(token, fixed["src/state.cpp"])
        self.assertIn("return void(hulk_note_failure(__LINE__))", fixed["src/state.cpp"])
        self.assertNotIn("HULK/PM4-FIRST-FAILURE", fixture()["src/state.cpp"])

    def test_d140_dirty_slots_after_scanner_does_not_remove_diagnostic(self):
        src=fixture()
        src["src/state.cpp"]=src["src/state.cpp"].replace(
            "bool FetchSlotDirty() {}", "uint64_t BitReverse() {}")
        fixed,changed=patch(src)
        self.assertTrue(changed)
        self.assertEqual(patch(fixed),(fixed,False))
        self.assertIn("hulk_note_failure(__LINE__)",fixed["src/state.cpp"])
        self.assertIn("HULK/PM4-LAST-GOOD",fixed["src/state.cpp"])
        self.assertIn("uint64_t BitReverse() {}",fixed["src/state.cpp"])

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
            "  if (header == 3) out.complete = false;\n", "")
        input_files["src/state.cpp"] = input_files["src/state.cpp"].replace(
            "  if (header == 2) out.complete = false;\n", "")
        input_files["src/state.cpp"] = input_files["src/state.cpp"].replace(
            "  if (header == 1) out.complete = false;\n", "")
        with self.assertRaisesRegex(ValueError, "failure sites unexpectedly 2"):
            patch(input_files)  # two sites, below the real three-site floor

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "No standalone C++ compiler")
    def test_first_failure_cpp_exact_generated_lambda(self):
        # The test builds the REAL inserted scanner header and first-failure
        # lambda, not a duplicate reimplementation. The callback must work
        # in return-void expressions and keep the first nested-IB failure.
        from patch_native_x_hulk_pm4_first_failure import SCAN_HEAD_NEW
        prefix = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
struct PacketScan {
  bool complete = true;
  uint32_t hulk_fail_site = 0;
  uint32_t hulk_fail_packet = 0;
  uint32_t hulk_fail_header = 0;
  uint32_t hulk_fail_depth = 0;
  uint32_t hulk_fail_remaining = 0;
  uint32_t hulk_last_good_begin = 0;
  uint32_t hulk_last_good_end = 0;
  uint32_t hulk_last_good_header = 0;
};
uint32_t Load32(const uint8_t* base, uint32_t p) {
  uint32_t value;
  std::memcpy(&value, base + p, sizeof(value));
  return value;
}
void ScanPackets(const uint8_t* base, uint32_t begin, uint32_t end,
                 PacketScan& out, int depth) {
"""
        suffix = r"""
    p += 4;
    if (header == 0xdeadbeefu)
      return void(hulk_note_failure(__LINE__));
    switch (header >> 30) {
      default:
        break;
""" + SCAN_TAIL_NEW + r"""
int main() {
  uint32_t data[] = {0x11223344u, 0x55667788u, 0xdeadbeefu, 0x12345678u};
  PacketScan scan;
  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  ScanPackets(bytes, 0, 16, scan, 2);
  assert(!scan.complete);
  assert(scan.hulk_fail_site != 0);
  assert(scan.hulk_fail_header == 0xdeadbeefu);
  assert(scan.hulk_fail_packet == 8);
  assert(scan.hulk_last_good_begin == 4);
  assert(scan.hulk_last_good_end == 8);
  assert(scan.hulk_last_good_header == 0x55667788u);
  assert(scan.hulk_fail_remaining == 4);
  assert(scan.hulk_fail_depth == 2);
  const uint32_t first_site = scan.hulk_fail_site;
  ScanPackets(bytes, 0, 16, scan, 7);
  assert(scan.hulk_fail_site == first_site && scan.hulk_fail_depth == 2);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "first_failure.cpp"
            target = root / ("first_failure.exe" if os.name == "nt" else "first_failure")
            source.write_text(prefix + SCAN_HEAD_NEW + suffix, encoding="utf-8")
            compiler = shutil.which("clang++") or shutil.which("g++")
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                            "-Werror", str(source), "-o", str(target)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(target)], check=True, capture_output=True,
                           text=True)

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
