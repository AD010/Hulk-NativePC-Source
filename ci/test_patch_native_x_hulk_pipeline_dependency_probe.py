#!/usr/bin/env python3
"""Source-only tests of the opt-in King Kong-style pipeline wait comparison."""
import unittest
from patch_native_x_hulk_pipeline_dependency_probe import (
    CVAR_OLD, CVAR_NEW, DRAW_OLD, DRAW_NEW, patch_text
)

class PipelineDependencyProbeTests(unittest.TestCase):
    def test_atomic_and_idempotent(self):
        original = CVAR_OLD + "\n" + DRAW_OLD
        patched, changed = patch_text(original)
        self.assertTrue(changed)
        self.assertEqual(patch_text(patched), (patched, False))
        self.assertEqual(patched.count(CVAR_NEW), 1)
        self.assertEqual(patched.count(DRAW_NEW), 1)
        with self.assertRaises(ValueError):
            patch_text(CVAR_NEW + CVAR_OLD + "\n" + DRAW_OLD)
        with self.assertRaises(ValueError):
            patch_text(CVAR_OLD + "\n" + DRAW_NEW)
        with self.assertRaises(ValueError):
            patch_text("unknown layout")

    def test_diagnostic_is_off_by_default(self):
        self.assertIn("native_x_renderer_hulk_wait_all_pipelines, false", CVAR_NEW)
        self.assertIn("hulk_wait_all ||", DRAW_NEW)
        self.assertIn("&& offscreen && !steady", DRAW_NEW)
        self.assertIn("steady && !hulk_wait_all", DRAW_NEW)
        self.assertIn("g_draw->SetWaitForPipelines(hulk_wait, steady && !hulk_wait_all)", DRAW_NEW)
        # No fake shader, GPU state, RTT alias or converted index data.
        for bad in ("force_shader", "fake_index", "memset", "edram_alias", "vs_program_override"):
            self.assertNotIn(bad, DRAW_NEW)

    def test_default_policy_is_unchanged(self):
        def old(offscreen, steady, wait_setting):
            return wait_setting and offscreen and not steady, steady
        def new(offscreen, steady, wait_setting, wait_all):
            return wait_all or (wait_setting and offscreen and not steady), steady and not wait_all
        for o in (False, True):
            for s in (False, True):
                for setting in (False, True):
                    self.assertEqual(old(o, s, setting), new(o, s, setting, False))
                    self.assertEqual(new(o, s, setting, True), (True, False))

if __name__ == "__main__":
    unittest.main()
