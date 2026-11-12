#!/usr/bin/env python3
"""Source-only regression: no per-draw three-second freeze for Hulk world."""
import unittest
from patch_native_x_hulk_scene_wait_budget import (
    DRAW_NEW, DRAW_OLD, FIELDS_NEW, FIELDS_OLD, HEADER_NEW, HEADER_OLD,
    INC_OLD, LOOP_NEW, LOOP_OLD, RENDER_NEW, RENDER_OLD, patch,
)

def fixture():
    return {
        "src/renderer.cpp": "/* HULK/SCENE-PIPELINE-WAIT */\n" + RENDER_OLD,
        "src/draw.h": INC_OLD + "\n" + HEADER_OLD + "\n" + FIELDS_OLD,
        "src/draw.cpp": DRAW_OLD + "\n" + LOOP_OLD + "\n" + LOOP_OLD,
    }

class SceneBudgetTests(unittest.TestCase):
    def test_atomic_and_idempotent(self):
        original = fixture()
        patched, changed = patch(original)
        self.assertTrue(changed)
        self.assertEqual(patch(patched), (patched, False))
        self.assertIn(RENDER_NEW, patched["src/renderer.cpp"])
        self.assertIn(HEADER_NEW, patched["src/draw.h"])
        self.assertIn(FIELDS_NEW, patched["src/draw.h"])
        self.assertIn("hulk_scene_wait_start_ + std::chrono::milliseconds(200)",
                      patched["src/draw.cpp"])
        self.assertEqual(patched["src/draw.cpp"].count(LOOP_NEW), 2)
        self.assertNotIn(LOOP_OLD, patched["src/draw.cpp"])
        self.assertIn("wait_start + kPipelineWaitLimit", patched["src/draw.cpp"])

    def test_partial_installs_and_source_drift_fail_closed(self):
        original = fixture()
        with self.assertRaises(ValueError):
            patch({"src/draw.h": original["src/draw.h"]})
        original["src/draw.cpp"] = original["src/draw.cpp"].replace(
            LOOP_OLD, "different_loop", 1)
        with self.assertRaises(ValueError):
            patch(original)
        installed, _ = patch(fixture())
        installed["src/draw.cpp"] = fixture()["src/draw.cpp"]
        with self.assertRaisesRegex(ValueError, "Partially"):
            patch(installed)

    def test_shared_budget_model_never_multiplies_per_draw(self):
        # This model verifies a deadline shared by the whole presentation
        # frame. The real C++ uses steady_clock and frames_presented().
        def deadline(frame_start_ms, draw_start_ms, enabled):
            return min(draw_start_ms + 3000, frame_start_ms + 200) if enabled \
                else draw_start_ms + 3000
        self.assertEqual(deadline(100, 100, True), 300)
        self.assertEqual(deadline(100, 160, True), 300)
        self.assertEqual(deadline(100, 400, True), 300)
        self.assertEqual(deadline(100, 400, False), 3400)
        self.assertIn("device_->frames_presented()", DRAW_NEW)

    def test_production_preflight_uses_source_patch_as_single_authority(self):
        # Prevent repeating build #80/#81: if wait budget changes, the
        # integrated source gate must not keep a stale hardcoded duration.
        from pathlib import Path
        gate = (Path(__file__).resolve().parent /
                "test_native_x_patch_chain.py").read_text(encoding="utf-8")
        self.assertIn("HULK_SCENE_WAIT_CPP not in draw", gate)
        self.assertIn("from patch_native_x_hulk_scene_wait_budget import DRAW_NEW", gate)
        self.assertNotIn('"hulk_scene_wait_start_ + std::chrono::milliseconds(', gate)


if __name__ == "__main__":
    unittest.main()
