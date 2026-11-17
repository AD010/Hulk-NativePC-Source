#!/usr/bin/env python3
"""Original Xenia Edge SDK backport regressions (one coherent source-only suite).

No game bytes. The optional SDK_ROOT mode checks a pristine pinned ReXGlue
checkout, before any of the three staged adaptations are applied.
"""
from pathlib import Path
import sys
import unittest

from patch_sdk_hulk_xenia_edge import (
    INDEX_FILE, INDEX_OLD, INDEX_NEW, INDEX_MARKER, patch_index,
    MIP_FILE, MIP_MARKER, MIP_ADDR_OLD, MIP_ADDR_NEW, MIP_OFFSET_START,
    MIP_OFFSET_END, MIP_SHAPE_INSERT_OLD, MIP_SHAPE_INSERT_NEW, MIP_SHAPE_OLD, MIP_SHAPE_NEW, patch_mip,
    SCALAR_SHADER, SCALAR_HEADER, SCALAR_MARKER, SCALAR_START,
    SCALAR_END, SCALAR_NORMALIZE, SCALAR_EXPO_OLD, SCALAR_EXPO_NEW,
    SCALAR_INSERT, SCALAR_VERSION_OLD, SCALAR_VERSION_NEW, patch_scalar,
)

SDK_ROOT = Path(sys.argv[1]) if len(sys.argv) == 2 else None
sys.argv[:] = sys.argv[:1]

INDEX_FIXTURE = """
  static constexpr uint32_t kMinRequiredConvertedIndexBufferSize =
""" + INDEX_OLD + "\n"


class ConvertedIndexPoolTests(unittest.TestCase):
    def test_simd_padding_added_not_multiplied(self):
        fixed, modified = patch_index(INDEX_FIXTURE)
        self.assertTrue(modified)
        self.assertIn(INDEX_NEW, fixed)
        self.assertNotIn(INDEX_OLD, fixed)
        self.assertEqual(patch_index(fixed), (fixed, False))
        # Upstream source describes a 16-byte SIMD co-alignment allowance.
        max_indices = (0xffff - 2) * 3
        wrong = 4 * max_indices * 16
        correct = 4 * max_indices + 16
        self.assertGreater(wrong, 12_000_000)
        self.assertLess(correct, 800_000)
        self.assertGreater(wrong, correct * 15)

    def test_does_not_guess_new_sdk_layout(self):
        for source in [INDEX_FIXTURE.replace(INDEX_OLD, ""), INDEX_FIXTURE + INDEX_MARKER,
                       INDEX_FIXTURE.replace("* +XE_GPU", "+XE_GPU"),
                       INDEX_FIXTURE.replace(INDEX_OLD, INDEX_NEW) + "\n// " + INDEX_MARKER]:
            with self.assertRaises(ValueError):
                patch_index(source)

    @unittest.skipUnless(SDK_ROOT is not None, "Pinned ReXGlue source checked in Windows CI")
    def test_actual_pinned_sdk_has_matching_expression(self):
        source = (SDK_ROOT / INDEX_FILE).read_text(encoding="utf-8-sig")
        modified, changed = patch_index(source)
        self.assertTrue(changed)
        self.assertIn(INDEX_NEW, modified)


MIP_FIXTURE = MIP_ADDR_OLD + "\n" + MIP_OFFSET_START + """
uint32_t width, uint32_t height) {
""" + MIP_SHAPE_INSERT_OLD + """
  if (packed_mip < 3) {
    if (log2_width > log2_height) {
      x_blocks = 0;
    }
  } else {
    if (log2_width > log2_height) {
      y_blocks = 0;
    }
  }
""" + MIP_OFFSET_END + "\n}\n"


class PackedMipTests(unittest.TestCase):
    def test_base_page_wins_and_2x1_is_square(self):
        output, changed = patch_mip(MIP_FIXTURE)
        self.assertTrue(changed)
        self.assertIn(MIP_ADDR_NEW, output)
        self.assertEqual(output.count(MIP_SHAPE_NEW), 2)
        self.assertIn(MIP_MARKER, output)
        self.assertEqual(patch_mip(output), (output, False))
        self.assertFalse(1 > max(0, 1))  # 2x1 goes down the square path.
        self.assertTrue(2 > max(0, 1))   # 4x1 remains a wide rectangle.

    def test_partial_packed_tail_cannot_pass_idempotence(self):
        fixed, changed = patch_mip(MIP_FIXTURE)
        self.assertTrue(changed)
        # Regression: old gate accepted a third old-shape branch, or an
        # unrelated third new-shape branch, as "already patched".
        variants = [
            fixed.replace(MIP_SHAPE_NEW, MIP_SHAPE_OLD, 1),
            fixed.replace(MIP_SHAPE_NEW, MIP_SHAPE_NEW + "\\n" + MIP_SHAPE_NEW, 1),
            fixed.replace(MIP_SHAPE_INSERT_NEW, MIP_SHAPE_INSERT_OLD, 1),
            fixed.replace(MIP_MARKER, MIP_MARKER + MIP_MARKER, 1),
        ]
        for variant in variants:
            with self.subTest(variant=variant[:100]):
                with self.assertRaises(ValueError):
                    patch_mip(variant)

    def test_fails_closed_on_unrecognized_texture_layout(self):
        for src in [MIP_FIXTURE.replace(MIP_ADDR_OLD, ""), MIP_FIXTURE + MIP_MARKER,
                    MIP_FIXTURE.replace("if (log2_width > log2_height)",
                                    "if (is_wide)", 1)]:
            with self.assertRaises(ValueError):
                patch_mip(src)

    @unittest.skipUnless(SDK_ROOT is not None, "Pinned ReXGlue source in Windows CI")
    def test_real_pinned_texture_util(self):
        original = (SDK_ROOT / MIP_FILE).read_text(encoding="utf-8-sig")
        patched, changed = patch_mip(original)
        self.assertTrue(changed)
        self.assertEqual(patched.count(MIP_ADDR_NEW), 1)
        self.assertEqual(patched.count(MIP_SHAPE_NEW), 2)


SCALAR_BLOCK = SCALAR_START + """
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
""" + SCALAR_END
SCALAR_SOURCE = SCALAR_BLOCK + SCALAR_EXPO_OLD + "\n"


class ShaderScalarFetchTests(unittest.TestCase):
    def test_rewrites_all_32bit_normalizations_and_exponent(self):
        shader, header, changed = patch_scalar(SCALAR_SOURCE, SCALAR_VERSION_OLD)
        self.assertTrue(changed)
        self.assertEqual(header, SCALAR_VERSION_NEW)
        self.assertEqual(shader.count(SCALAR_MARKER), 1)
        self.assertEqual(shader.count("normalized_mul_op"), 4)
        self.assertIn("used_format_component_count > 1", shader)
        self.assertIn("spv::OpFMul", shader)
        self.assertIn(SCALAR_EXPO_NEW, shader)
        self.assertNotIn(SCALAR_EXPO_OLD, shader)
        self.assertEqual(patch_scalar(shader, header), (shader, header, False))

    def test_scalar_idempotence_rejects_partial_fetch_case(self):
        shader, header, changed = patch_scalar(SCALAR_SOURCE, SCALAR_VERSION_OLD)
        self.assertTrue(changed)
        self.assertEqual(patch_scalar(shader, header), (shader, header, False))
        # The old global-count check accepted a missing real normalization
        # when a decoy occurrence was added elsewhere in the file.
        damaged = shader.replace(
            "normalized_mul_op, result_type, result",
            "spv::OpVectorTimesScalar, result_type, result", 1)
        damaged += "\\n// normalized_mul_op\\n"
        with self.assertRaises(ValueError):
            patch_scalar(damaged, header)
        # A second cache version declaration must not masquerade as a
        # uniquely reviewed SDK header.
        with self.assertRaises(ValueError):
            patch_scalar(shader, header + "\\n" + SCALAR_VERSION_NEW)

    def test_wrong_or_partially_patched_sdk_fails(self):
        for source, header in [
            (SCALAR_SOURCE, "    static constexpr uint32_t kVersion = 11;"),
            (SCALAR_SOURCE, SCALAR_VERSION_NEW),
            (SCALAR_SOURCE.replace(SCALAR_START, ""), SCALAR_VERSION_OLD),
            (SCALAR_SOURCE.replace("spv::OpVectorTimesScalar, result_type, result",
                            "spv::OpFMul, result_type, result", 1), SCALAR_VERSION_OLD),
            (SCALAR_SOURCE + SCALAR_MARKER, SCALAR_VERSION_OLD),
            (SCALAR_SOURCE.replace(SCALAR_EXPO_OLD, ""), SCALAR_VERSION_OLD),
        ]:
            with self.subTest(source=source[:42], header=header):
                with self.assertRaises(ValueError):
                    patch_scalar(source, header)

    @unittest.skipUnless(SDK_ROOT is not None, "Real ReXGlue source checked only in Windows CI")
    def test_real_pinned_sdk_layout(self):
        shader = (SDK_ROOT / SCALAR_SHADER).read_text(encoding="utf-8-sig")
        header = (SDK_ROOT / SCALAR_HEADER).read_text(encoding="utf-8-sig")
        updated, version, changed = patch_scalar(shader, header)
        self.assertTrue(changed)
        self.assertIn(SCALAR_MARKER, updated)
        self.assertIn(SCALAR_VERSION_NEW, version)
        self.assertEqual(updated.count("normalized_mul_op"), 4)

if __name__ == "__main__":
    unittest.main()
