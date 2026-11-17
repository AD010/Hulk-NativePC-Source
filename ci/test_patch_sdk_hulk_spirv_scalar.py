#!/usr/bin/env python3
"""Regression for Xenia Edge 09d405f scalar OpVectorTimesScalar NVIDIA crash.

Uses synthetic shader source, never original proprietary Hulk bytecode.
When CI supplies a pinned SDK directory, additionally validate its current
real translator source before the patch is installed.
"""
from pathlib import Path
import sys
import unittest

from patch_sdk_hulk_spirv_scalar import (
    SHADER, HEADER, MARKER, START, END, NORMALIZE, EXPO_OLD, EXPO_NEW,
    INSERT, VERSION_OLD, VERSION_NEW, patch,
)

SDK_ROOT = Path(sys.argv[1]) if len(sys.argv) == 2 else None
sys.argv[:] = sys.argv[:1]

BLOCK = START + """
      assert_true(used_format_components == needed_words);
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          result = builder_->createNoContractionBinOp(
              spv::OpVectorTimesScalar, result_type, result, one);
          result = builder_->createNoContractionBinOp(
              spv::OpVectorTimesScalar, result_type, result, two);
        } else {
          result = builder_->createNoContractionBinOp(
              spv::OpVectorTimesScalar, result_type, result, three);
        }
      }
""" + END
SOURCE = BLOCK + EXPO_OLD + "\n"


class ShaderScalarFetchTests(unittest.TestCase):
    def test_rewrites_all_32bit_normalizations_and_exponent(self):
        shader, header, changed = patch(SOURCE, VERSION_OLD)
        self.assertTrue(changed)
        self.assertEqual(header, VERSION_NEW)
        self.assertEqual(shader.count(MARKER), 1)
        self.assertEqual(shader.count("normalized_mul_op"), 4)
        self.assertIn("used_format_component_count > 1", shader)
        self.assertIn("spv::OpFMul", shader)
        self.assertIn(EXPO_NEW, shader)
        self.assertNotIn(EXPO_OLD, shader)
        self.assertEqual(patch(shader, header), (shader, header, False))

    def test_wrong_or_partially_patched_sdk_fails(self):
        for source, header in [
            (SOURCE, "    static constexpr uint32_t kVersion = 11;"),
            (SOURCE, VERSION_NEW),
            (SOURCE.replace(START, ""), VERSION_OLD),
            (SOURCE.replace("spv::OpVectorTimesScalar, result_type, result",
                            "spv::OpFMul, result_type, result", 1), VERSION_OLD),
            (SOURCE + MARKER, VERSION_OLD),
            (SOURCE.replace(EXPO_OLD, ""), VERSION_OLD),
        ]:
            with self.subTest(source=source[:42], header=header):
                with self.assertRaises(ValueError):
                    patch(source, header)

    @unittest.skipUnless(SDK_ROOT is not None, "Real ReXGlue source checked only in Windows CI")
    def test_real_pinned_sdk_layout(self):
        shader = (SDK_ROOT / SHADER).read_text(encoding="utf-8-sig")
        header = (SDK_ROOT / HEADER).read_text(encoding="utf-8-sig")
        updated, version, changed = patch(shader, header)
        self.assertTrue(changed)
        self.assertIn(MARKER, updated)
        self.assertIn(VERSION_NEW, version)
        self.assertEqual(updated.count("normalized_mul_op"), 4)


if __name__ == "__main__":
    unittest.main()
