#!/usr/bin/env python3
"""Synthetic scene draw order and shader wait regression, no Xbox game bytes."""
import unittest
from patch_native_x_hulk_scene_pipeline_wait import (
    DECL_ANCHOR, DECL_NEW, WAIT_ANCHOR, WAIT_NEW, patch_text,
)

class SceneWaitTests(unittest.TestCase):
    def test_atomic_idempotent_install(self):
        original = DECL_ANCHOR + "\n" + WAIT_ANCHOR
        updated, changed = patch_text(original)
        self.assertTrue(changed)
        self.assertEqual(patch_text(updated), (updated, False))
        self.assertIn("native_x_renderer_hulk_wait_scene_pipelines", updated)
        self.assertIn("HULK/SCENE-PIPELINE-WAIT", updated)
        for incomplete in (DECL_NEW + "\n" + WAIT_ANCHOR,
                           DECL_ANCHOR + "\n" + WAIT_NEW, "unknown upstream"):
            with self.assertRaises(ValueError):
                patch_text(incomplete)

    def test_opt_in_only_and_no_silent_drop_for_scene(self):
        self.assertIn("false, ", DECL_NEW)  # defaults off in all other games
        self.assertIn("hulk_wait_scene ||", WAIT_NEW)
        self.assertIn("native_x_renderer_wait_offscreen_pipelines", WAIT_NEW)
        self.assertIn("steady && !hulk_wait_scene", WAIT_NEW)
        self.assertNotIn("skip_shader", WAIT_NEW)
        self.assertNotIn("CreateGraphicsPipelineState", WAIT_NEW)
        def wait(opt_in, upstream, offscreen, steady):
            return opt_in or (upstream and offscreen and not steady)
        self.assertFalse(wait(False, True, False, False))
        self.assertTrue(wait(True, True, False, False))
        self.assertTrue(wait(True, False, True, True))
        self.assertTrue(wait(False, True, True, False))
        self.assertFalse(wait(False, True, True, True))
        self.assertFalse(wait(False, False, True, False))

if __name__ == "__main__":
    unittest.main()
