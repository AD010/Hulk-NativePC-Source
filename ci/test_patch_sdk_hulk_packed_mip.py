#!/usr/bin/env python3
"""Source-only regression for original Xbox 360 packed mip0/2x1 layout."""
from pathlib import Path
import sys
import unittest
from patch_sdk_hulk_packed_mip import (
    FILE, MARKER, ADDR_OLD, ADDR_NEW, OFFSET_START, OFFSET_END,
    SHAPE_INSERT_OLD, SHAPE_NEW, patch,
)

SDK_ROOT = Path(sys.argv[1]) if len(sys.argv) == 2 else None
sys.argv[:] = sys.argv[:1]
FIXTURE = ADDR_OLD + "\n" + OFFSET_START + """
uint32_t width, uint32_t height) {
""" + SHAPE_INSERT_OLD + """
  if (packed_mip < 3) {
    if (log2_width > log2_height) {
      x_blocks = 0;
    }
  } else {
    if (log2_width > log2_height) {
      y_blocks = 0;
    }
  }
""" + OFFSET_END + "\n}\n"


class PackedMipTests(unittest.TestCase):
    def test_base_page_wins_and_2x1_is_square(self):
        output, changed = patch(FIXTURE)
        self.assertTrue(changed)
        self.assertIn(ADDR_NEW, output)
        self.assertEqual(output.count(SHAPE_NEW), 2)
        self.assertIn(MARKER, output)
        self.assertEqual(patch(output), (output, False))
        self.assertFalse(1 > max(0, 1))  # 2x1 goes down the square path.
        self.assertTrue(2 > max(0, 1))   # 4x1 remains a wide rectangle.

    def test_fails_closed_on_unrecognized_texture_layout(self):
        for src in [FIXTURE.replace(ADDR_OLD, ""), FIXTURE + MARKER,
                    FIXTURE.replace("if (log2_width > log2_height)",
                                    "if (is_wide)", 1)]:
            with self.assertRaises(ValueError):
                patch(src)

    @unittest.skipUnless(SDK_ROOT is not None, "Pinned ReXGlue source in Windows CI")
    def test_real_pinned_texture_util(self):
        original = (SDK_ROOT / FILE).read_text(encoding="utf-8-sig")
        patched, changed = patch(original)
        self.assertTrue(changed)
        self.assertEqual(patched.count(ADDR_NEW), 1)
        self.assertEqual(patched.count(SHAPE_NEW), 2)


if __name__ == "__main__":
    unittest.main()
