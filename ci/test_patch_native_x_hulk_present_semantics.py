#!/usr/bin/env python3
"""Regression: logical Swap frame-end retains Present.r4 front fetch."""
import unittest
from patch_native_x_hulk_present_semantics import OLD, NEW, PRESENT_R4, patch_text

class TestHulkPresentSemantics(unittest.TestCase):
    def test_source_patch_preserves_front_setter(self):
        baseline = "void f() {\n" + PRESENT_R4 + "\n" + OLD + "\n}"
        result, changed = patch_text(baseline)
        self.assertTrue(changed)
        self.assertIn(PRESENT_R4, result)
        self.assertIn("[HULK/NATIVEX] present", result)
        self.assertEqual(patch_text(result), (result, False))
    def test_missing_setter_refused(self):
        with self.assertRaises(ValueError):
            patch_text(OLD)
    def test_stale_split_swap_refused(self):
        with self.assertRaises(ValueError):
            patch_text(PRESENT_R4 + "\n if (call == Call::kSwap) {\n" + OLD)
    def test_unknown_compose_refused(self):
        with self.assertRaises(ValueError):
            patch_text(PRESENT_R4 + "\n changed renderer")
    def test_original_eor_worker_bridge_is_unique_and_guarded(self):
        from patch_native_x_hulk_present_semantics import (
            patch_worker_sources, API_BEFORE, INTERNAL_BEFORE,
            DISPATCH_BEFORE, WORKER_DEVICE_BEFORE, WORKER_SETTER_BEFORE,
            WORKER_PRESENT_BEFORE,
        )
        fixtures = {
            "include/native_x_renderer/native_x_renderer.h": API_BEFORE,
            "src/renderer.h": INTERNAL_BEFORE,
            "src/native_x_renderer.cpp": DISPATCH_BEFORE,
            "src/renderer.cpp": (
                "#include <algorithm>\n" + WORKER_DEVICE_BEFORE +
                "\n" + WORKER_SETTER_BEFORE + "\n" + WORKER_PRESENT_BEFORE
            ),
        }
        modified = patch_worker_sources(fixtures)
        self.assertEqual(patch_worker_sources(modified), modified)
        header = modified["include/native_x_renderer/native_x_renderer.h"]
        renderer = modified["src/renderer.cpp"]
        self.assertIn("kBackbufferReady", header)
        self.assertIn("kFailedAfterGpu", header)
        self.assertIn("!g_present_with_swaps && !g_frame_generation", renderer)
        self.assertLess(renderer.index("callback(*g_device)"),
                        renderer.index("FinishPresent();"))
        self.assertLess(renderer.index("FinishPresent();"),
                        renderer.index("FrameImage* image = with_swap"))
        self.assertIn("if (result != HulkEorPresentResult::kNotReady)", renderer)
        self.assertIn("g_device->FinishHiddenFrame();", renderer)
        self.assertIn("return;", renderer)
        self.assertNotIn("device_->Present();", renderer)
        self.assertIn("renderer::SetHulkEorPresentCallback(callback)", modified["src/native_x_renderer.cpp"])
        self.assertEqual(renderer.count("HulkEorPresentResult result = callback"), 1)

    def test_worker_patch_rejects_changed_present_ownership(self):
        from patch_native_x_hulk_present_semantics import (
            patch_worker_sources, API_BEFORE, INTERNAL_BEFORE,
            DISPATCH_BEFORE, WORKER_DEVICE_BEFORE, WORKER_SETTER_BEFORE,
        )
        corrupted = {
            "include/native_x_renderer/native_x_renderer.h": API_BEFORE,
            "src/renderer.h": INTERNAL_BEFORE,
            "src/native_x_renderer.cpp": DISPATCH_BEFORE,
            "src/renderer.cpp": (
                "#include <algorithm>\n" + WORKER_DEVICE_BEFORE +
                "\n" + WORKER_SETTER_BEFORE + "\n" +
                "FrameImage* unexpected = nullptr;"
            ),
        }
        with self.assertRaisesRegex(ValueError, "source anchor changed"):
            patch_worker_sources(corrupted)

if __name__ == "__main__":
    unittest.main()
