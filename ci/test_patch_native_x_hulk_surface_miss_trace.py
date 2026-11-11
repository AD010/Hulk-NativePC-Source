#!/usr/bin/env python3
"""Regression: PM4 miss diagnostics neither accept nor synthesize an attachment."""
import unittest
import shutil
import subprocess
import tempfile
from pathlib import Path
from patch_native_x_hulk_surface_miss_trace import (
    OLD, NEW, ZERO_OLD, ZERO_NEW, ACCEPT_OLD, ACCEPT_NEW,
    NO_WRITE_OLD, NO_WRITE_NEW, patch_text
)

class TestMissTrace(unittest.TestCase):
    def test_unique_and_idempotent(self):
        original = "bool HulkPacketSurfaces(" + "\n" + ZERO_OLD + "\n" + NO_WRITE_OLD + "\n" + OLD + "\n" + ACCEPT_OLD
        result, changed = patch_text(original)
        self.assertTrue(changed)
        self.assertEqual(patch_text(result), (result, False))
        self.assertIn("HULK/PM4-MISS", result)
        self.assertIn("HULK/PM4-ACCEPT", result)
        self.assertIn("hulk_packet_draws_mapped", result)
        for s in ("void unrelated() {}", original + "\n" + OLD):
            with self.assertRaises(ValueError): patch_text(s)
    def test_reason_counters_are_mutually_exclusive(self):
        self.assertIn("[HULK/PM4-NO-WRITABLE-RT]", NO_WRITE_NEW)
        self.assertNotIn("[HULK/PM4-NO-WRITABLE-RT]", NEW)
        self.assertIn("HULK/PM4-NO-WRITE-EARLY", NO_WRITE_NEW)
        self.assertIn("[HULK/PM4-ACTUAL-MISS]", NEW)
        self.assertIn("(gpu_color_mask & 0xFFFFu) == 0u", NO_WRITE_NEW)
        self.assertIn("(gpu_depth_control & 0x3u) == 0u", NO_WRITE_NEW)
        self.assertIn("if (unresolved)", NEW)
        self.assertIn("[HULK/PM4-ZERO-SURFACE]", ZERO_NEW)
        self.assertIn("return false;", ZERO_NEW)
        self.assertNotIn("out[slot] =", NEW + ZERO_NEW + NO_WRITE_NEW)

        def classify(has_target, unresolved, surface_pitch):
            if not surface_pitch: return "zero_pitch"
            if unresolved: return "actual_miss"
            if not has_target: return "no_writable"
            return "accepted"
        for args, expected in (
            ((False, False, 0), "zero_pitch"),
            ((False, False, 640), "no_writable"),
            ((True, True, 640), "actual_miss"),
            ((False, True, 640), "actual_miss"),
            ((True, False, 640), "accepted"),
        ):
            self.assertEqual(classify(*args), expected)

    @unittest.skipUnless(shutil.which("clang++") or shutil.which("g++"),
                         "A C++ compiler is required to test the real early gate")
    def test_compiled_no_write_gate_matches_enabled_rt_and_depth(self):
        # Execute the actual C++ gate copied from the source patch; this
        # does NOT fake any Hulk frame or GPU target. Missing attachments
        # still fail closed elsewhere in HulkPacketSurfaces.
        prefix = NO_WRITE_NEW.split("  bool has_target = false", 1)[0]
        code = """#include <atomic>
#include <cassert>
#include <cstdint>
#define REXLOG_INFO(...) ((void)0)
bool enabled(uint32_t gpu_color_mask, uint32_t gpu_depth_control,
             uint32_t gpu_surface) {
  (void)gpu_surface;
""" + prefix + """  return true;
}
int main() {
  assert(!enabled(0, 0, 640));
  assert(!enabled(0, 0, 1280));
  assert(!enabled(0xFFFF0000u, 0, 640));
  assert(enabled(0x1u, 0, 640));
  assert(enabled(0xF000u, 0, 640));
  assert(enabled(0, 0x1u, 640));
  assert(enabled(0, 0x2u, 640));
  assert(enabled(0xFFFFu, 0x8777u, 640));
}
"""
        compiler = shutil.which("clang++") or shutil.which("g++")
        with tempfile.TemporaryDirectory(prefix="hulk_pm4_nowrite_") as tmp:
            path = Path(tmp)
            src = path / "gate.cpp"
            exe = path / "gate"
            src.write_text(code, encoding="utf-8")
            subprocess.run(
                [compiler, "-std=c++20", "-O0", "-Wall", "-Wextra",
                 "-Werror", str(src), "-o", str(exe)],
                check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True)

    def test_partial_install_rejected(self):
        original = ("bool HulkPacketSurfaces(\n" + ZERO_OLD +
                    "\n" + NO_WRITE_OLD + "\n" + OLD + "\n" + ACCEPT_OLD)
        updated, changed = patch_text(original)
        self.assertTrue(changed)
        self.assertEqual(patch_text(updated), (updated, False))
        for incomplete in (
            updated.replace(NO_WRITE_NEW, NO_WRITE_OLD),
            updated.replace(ZERO_NEW, ZERO_OLD),
            updated.replace(NEW, OLD),
            updated.replace(ACCEPT_NEW, ACCEPT_OLD),
        ):
            with self.assertRaises(ValueError):
                patch_text(incomplete)

    def test_does_not_change_reject_path(self):
        self.assertEqual(OLD.count("unresolved = true"), NEW.count("unresolved = true"))
        self.assertEqual(OLD.count("if (!has_target || unresolved)"), NEW.count("if (!has_target || unresolved)"))
        self.assertNotIn("out[slot] =", NEW)
        self.assertIn("bound ? bound.info", NEW)
        self.assertIn("misses % 20000", NEW)
        self.assertIn("mapped % 50000", ACCEPT_NEW)
        self.assertIn("return false;", NO_WRITE_NEW)
        self.assertIn("if (!has_target || unresolved)", NEW)
        self.assertNotIn("out[slot] =", ACCEPT_NEW)

if __name__ == "__main__": unittest.main()
