#!/usr/bin/env python3
"""Focused regression tests for a strict, idempotent Vulkan fetch patch."""
import unittest
from patch_sdk_hulk_spirv_fetch import (
    WORD4, EXP_OLD, COMMENT_OLD, VERSION_OLD, VERSION_NEW, MARKER, patch
)

SOURCE = WORD4 + "\n" + EXP_OLD + "\n" + COMMENT_OLD + "\n"
HEADER = VERSION_OLD + "\n"


class SpirvFetchPatchTests(unittest.TestCase):
    def test_word3_exponent_and_cache_invalidation(self):
        shader, header, changed = patch(SOURCE, HEADER)
        self.assertTrue(changed)
        self.assertIn(MARKER, shader)
        self.assertIn("fetch_constant_word_3_signed", shader)
        self.assertIn("(fetch_constant_word_0_index + 3)", shader)
        self.assertNotIn(EXP_OLD, shader)
        self.assertIn(VERSION_NEW, header)
        shader2, header2, changed2 = patch(shader, header)
        self.assertFalse(changed2)
        self.assertEqual((shader2, header2), (shader, header))

    def test_refuses_unknown_sdk(self):
        with self.assertRaises(ValueError):
            patch(SOURCE.replace(WORD4, ""), HEADER)
        with self.assertRaises(ValueError):
            patch(SOURCE, "static constexpr uint32_t kVersion = 123;")

    def test_refuses_partial_patch(self):
        with self.assertRaises(ValueError):
            patch(MARKER + "\n" + SOURCE, HEADER)


if __name__ == "__main__":
    unittest.main()
