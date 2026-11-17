#!/usr/bin/env python3
"""Synthetic source-only regression: converted index buffer page 16x excess."""
from pathlib import Path
import sys
import unittest
from patch_sdk_hulk_index_pool import FILE, OLD, NEW, MARKER, patch

SDK_ROOT = Path(sys.argv[1]) if len(sys.argv) == 2 else None
sys.argv[:] = sys.argv[:1]
FIXTURE = """
  static constexpr uint32_t kMinRequiredConvertedIndexBufferSize =
""" + OLD + "\n"


class ConvertedIndexPoolTests(unittest.TestCase):
    def test_simd_padding_added_not_multiplied(self):
        fixed, modified = patch(FIXTURE)
        self.assertTrue(modified)
        self.assertIn(NEW, fixed)
        self.assertNotIn(OLD, fixed)
        self.assertEqual(patch(fixed), (fixed, False))
        # Upstream source describes a 16-byte SIMD co-alignment allowance.
        max_indices = (0xffff - 2) * 3
        wrong = 4 * max_indices * 16
        correct = 4 * max_indices + 16
        self.assertGreater(wrong, 12_000_000)
        self.assertLess(correct, 800_000)
        self.assertGreater(wrong, correct * 15)

    def test_does_not_guess_new_sdk_layout(self):
        for source in [FIXTURE.replace(OLD, ""), FIXTURE + MARKER,
                       FIXTURE.replace("* +XE_GPU", "+XE_GPU")]:
            with self.assertRaises(ValueError):
                patch(source)

    @unittest.skipUnless(SDK_ROOT is not None, "Pinned ReXGlue source checked in Windows CI")
    def test_actual_pinned_sdk_has_matching_expression(self):
        source = (SDK_ROOT / FILE).read_text(encoding="utf-8-sig")
        modified, changed = patch(source)
        self.assertTrue(changed)
        self.assertIn(NEW, modified)


if __name__ == "__main__":
    unittest.main()
