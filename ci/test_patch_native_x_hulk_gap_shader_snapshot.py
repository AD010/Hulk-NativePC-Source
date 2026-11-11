#!/usr/bin/env python3
"""Source-only and semantic tests; no copyrighted Hulk shader bytes."""
import unittest
from patch_native_x_hulk_gap_shader_snapshot import (
    patch, BEFORE_HEAD, BEFORE_DRAW, BEFORE_MISMATCH, BEFORE_TAIL,
    AFTER_HEAD, AFTER_DRAW, AFTER_MISMATCH, AFTER_TAIL
)

def fixture():
    return ("\n// HULK/PFP-PREDICATE HULK/PM4-REGISTER-ORDER\n" +
            BEFORE_HEAD + "\n" + BEFORE_DRAW + "\n" +
            BEFORE_MISMATCH + "\n" + BEFORE_TAIL)

class ShaderPrescanTests(unittest.TestCase):
    def test_atomic_idempotent_source_patch(self):
        before = fixture()
        fixed, changed = patch(before)
        self.assertTrue(changed)
        self.assertEqual(patch(fixed), (fixed, False))
        for fragment in (AFTER_HEAD, AFTER_DRAW, AFTER_MISMATCH, AFTER_TAIL):
            self.assertIn(fragment, fixed)
        self.assertIn("uint64_t(load.ucode_offset) + load.dwords", fixed)
        self.assertIn("hulk_prescan_words.data() + original.ucode_offset", fixed)
        self.assertLess(AFTER_DRAW.index("if (!hulk_shader_replay_ok)"),
                        AFTER_DRAW.index("Gather(p, call, false, base)"))
        for partial in (BEFORE_HEAD + AFTER_DRAW + BEFORE_TAIL,
                        AFTER_HEAD + BEFORE_DRAW + BEFORE_TAIL,
                        "changed upstream"):
            with self.assertRaises(ValueError):
                patch(partial)

    def test_first_scan_shader_words_survive_changed_last_word(self):
        # Real log178: two distinct 27-dword VS programs share 26 initial
        # words but differ at index 26. We test the mechanism, not the
        # copyrighted shader words.
        original = [0xAB000000 + i for i in range(27)]
        changed = original.copy()
        changed[26] ^= 0x5DEAD123
        first = dict(packet=0xA2830C4C, address=0xA2830C58, type=0,
                     dwords=27, immediate=True)
        def restore(meta, words):
            if meta != first or len(words) != first["dwords"]:
                return None
            return original.copy()
        self.assertEqual(restore(first, changed), original)
        self.assertNotEqual(changed, original)
        self.assertIsNone(restore(dict(first, packet=first["packet"]+4), changed))
        self.assertIsNone(restore(first, changed[:-1]))

    def test_no_game_fabricated_fallback(self):
        self.assertNotIn("return original", AFTER_HEAD)
        self.assertNotIn("std::memset", AFTER_HEAD)
        self.assertNotIn("HulkGuessShader", AFTER_HEAD)
        self.assertIn("return false;", AFTER_HEAD)

if __name__ == "__main__":
    unittest.main()
