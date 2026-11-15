#!/usr/bin/env python3
"""Source-only regression for exact Xenos 2D fragment computed LOD."""
import unittest
from patch_xenosrecomp_hulk_fetch import INCLUDE, OLD, patch as strict
from patch_xenosrecomp_hulk_lod import (
    patch_sources, LODGATE, SWITCH, SWITCH_NEW, HELPER, HELPER_ANCHOR, MARKER, EMIT_OLD, EMIT_NEW)


class TestHulkLod(unittest.TestCase):
    def test_pixel_only_unclamped_no_silent_drop(self):
        cpp, _ = strict(INCLUDE + '\n' + OLD)
        cpp += '\n' + SWITCH + '\n' + EMIT_OLD
        hdr = HELPER_ANCHOR
        c, h, changed = patch_sources(cpp, hdr)
        self.assertTrue(changed)
        self.assertEqual(patch_sources(c, h), (c, h, False))
        self.assertIn('CalculateLevelOfDetailUnclamped', h)
        self.assertIn('isPixelShader && instr.dimension == TextureDimension::Texture2D', c)
        self.assertIn('case FetchOpcode::GetTextureComputedLod:', c)
        self.assertIn('throw std::runtime_error', c)
        self.assertIn(MARKER, c)
        self.assertIn(EMIT_NEW, c)
        self.assertIn('float4(lod, 0.0, 0.0, 0.0)', h)
        self.assertNotIn('max(0', h)

    def test_unsupported_stays_fail_closed(self):
        with self.assertRaises(ValueError):
            patch_sources(OLD, HELPER_ANCHOR)
        c, _ = strict(INCLUDE + '\n' + OLD)
        with self.assertRaises(ValueError):
            patch_sources(c + '\n' + EMIT_OLD, HELPER)
        with self.assertRaises(ValueError):
            patch_sources(c + '\n' + EMIT_OLD, HELPER_ANCHOR)


if __name__ == '__main__':
    unittest.main()
