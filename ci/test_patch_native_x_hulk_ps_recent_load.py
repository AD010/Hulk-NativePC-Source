#!/usr/bin/env python3
"""Test cached first origin versus last executed Xenos pixel shader load."""
import unittest
from patch_native_x_hulk_ps_recent_load import (
    patch, HEADER_GETTER_OLD, HEADER_GETTER_NEW,
    HEADER_FIELD_OLD, HEADER_FIELD_NEW, LOAD_OLD, LOAD_NEW,
    RESET_OLD, RESET_NEW, RESET_D140_OLD, RESET_D140_NEW, LOG_OLD, LOG_NEW, ARGS_OLD, ARGS_NEW)


def fixture():
    return {
        "src/state.cpp": RESET_OLD + "\n" + LOAD_OLD + "\n",
        "src/state.h": HEADER_GETTER_OLD + "\n" + HEADER_FIELD_OLD + "\n",
        "src/renderer.cpp": "// HULK/PS-PROGRAM-ORIGIN\n" +
                            LOG_OLD + "\n" + ARGS_OLD + "\n",
    }


class RecentPixelLoadTests(unittest.TestCase):
    def test_atomic_idempotent_and_cache_fast_path(self):
        patched, changed = patch(fixture())
        self.assertTrue(changed)
        self.assertEqual(patch(patched), (patched, False))
        state = patched["src/state.cpp"]
        header = patched["src/state.h"]
        renderer = patched["src/renderer.cpp"]
        self.assertIn("if (load.type == 1)", state)
        self.assertIn("++hulk_ps_recent_serial_", state)
        self.assertIn("HULK/PS-RECENT-LOAD", state)
        self.assertIn("hulk_ps_recent_packet_ = 0;", state)
        self.assertIn("hulk_ps_recent_call() const", header)
        self.assertIn("last_load_serial={}", renderer)
        self.assertIn("tracker.hulk_ps_recent_packet()", renderer)
        self.assertNotIn("c.programs[1] =", renderer)
        self.assertIn("cache_first_packet", renderer)
        self.assertIn("last_load_packet", renderer)

    def test_partial_install_rejected_and_no_implicit_gpu_edit(self):
        d = fixture()
        d["src/state.cpp"] = LOAD_NEW + "\n" + RESET_OLD
        with self.assertRaises(ValueError):
            patch(d)
        d = fixture()
        del d["src/renderer.cpp"]
        with self.assertRaises(ValueError):
            patch(d)
        d = fixture()
        d["src/state.h"] = d["src/state.h"].replace(HEADER_FIELD_OLD, "")
        with self.assertRaises(ValueError):
            patch(d)
        for token in ("SendValue(", "Draw(", "programs_[1] =", "shader_ucode"):
            self.assertNotIn(token, LOAD_NEW)
        self.assertIn("if (load.type >= 2) continue;", LOAD_NEW)


if __name__ == "__main__":
    unittest.main()
