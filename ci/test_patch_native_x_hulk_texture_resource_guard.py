#!/usr/bin/env python3
"""Hulk native texture descriptor safety: source-only, no copyrighted data."""
import unittest
from patch_native_x_hulk_texture_resource_guard import (
    INCLUDE_OLD, INCLUDE_NEW, DESCRIPTOR_OLD, DESCRIPTOR_NEW, patch_text
)

def valid(width, height, depth, mips, cube=False, is_3d=False):
    extent = max(width, height, depth if is_3d else 1)
    max_mips = extent.bit_length()
    return (width > 0 and height > 0 and depth > 0 and
            not(cube and width != height) and
            0 < mips <= max_mips)

class ResourceGuardTests(unittest.TestCase):
    def test_source_patch_atomic_idempotent(self):
        source = INCLUDE_OLD + "\n" + DESCRIPTOR_OLD
        fixed, changed = patch_text(source)
        self.assertTrue(changed)
        self.assertEqual(patch_text(fixed), (fixed, False))
        self.assertIn("if (hulk_invalid_desc)", fixed)
        self.assertIn("return nullptr;", fixed)
        self.assertIn("HULK/TEXTURE-RESOURCE-GUARD", fixed)
        self.assertIn("no GPU allocation", fixed)
        with self.assertRaises(ValueError):
            patch_text(INCLUDE_NEW + "\n" + DESCRIPTOR_OLD)
        with self.assertRaises(ValueError):
            patch_text(INCLUDE_OLD + "\n" + DESCRIPTOR_NEW)
        with self.assertRaises(ValueError):
            patch_text("unknown source")

    def test_no_square_cube_fabrication(self):
        self.assertFalse(valid(1280, 720, 6, 11, cube=True))
        self.assertTrue(valid(1024, 1024, 6, 11, cube=True))
        self.assertTrue(valid(1280, 720, 1, 11))  # Ordinary 2D scene
        self.assertTrue(valid(1, 1, 6, 1, cube=True))

    def test_mips_and_dimensions_checked(self):
        self.assertFalse(valid(64, 64, 1, 9))
        self.assertTrue(valid(64, 64, 1, 7))
        self.assertFalse(valid(0, 720, 1, 4))
        self.assertFalse(valid(1, 1, 0, 1))
        self.assertFalse(valid(512, 512, 0, 4, is_3d=True))
        self.assertTrue(valid(64, 32, 16, 7, is_3d=True))
        self.assertFalse(valid(64, 32, 16, 8, is_3d=True))

    def test_regular_host_rendering_unaffected(self):
        self.assertIn("desc.Format = host_format.format;", DESCRIPTOR_NEW)
        self.assertNotIn("CreateTexture(desc", DESCRIPTOR_NEW)
        self.assertNotIn("Change", DESCRIPTOR_NEW)
        self.assertNotIn("memset", DESCRIPTOR_NEW)
        self.assertNotIn("placeholder_", DESCRIPTOR_NEW)

if __name__ == "__main__":
    unittest.main()
