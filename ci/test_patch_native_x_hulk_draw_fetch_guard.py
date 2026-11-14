#!/usr/bin/env python3
"""Regression tests for invalid texture/vertex fetches in Hulk log 185."""
import unittest
from patch_native_x_hulk_draw_fetch_guard import (
    patch, TEXTURE_OLD, TEXTURE_NEW, VERTEX_OLD, VERTEX_NEW,
    PROBE_OLD, PROBE_NEW, PROBE_CVAR_ANCHOR, PROBE_CVAR_NEW)

class HulkDrawFetchGuardTests(unittest.TestCase):
    def test_atomic_and_idempotent(self):
        original = ("#include <bit>\n" + TEXTURE_OLD + "\n" + VERTEX_OLD +
                    "\n" + PROBE_CVAR_ANCHOR + "\n" + PROBE_OLD)
        patched, changed = patch(original)
        self.assertTrue(changed)
        self.assertEqual(patch(patched), (patched, False))
        for marker in ("HULK/MIP-PROVENANCE", "hulk_mip_draws",
                       "REXCVAR_GET(native_x_renderer_hulk_mip_probe)",
                       "fetch={:#x}", "HULK/NATIVEX-DRAW-FETCH-GUARD",
                       "HULK/NATIVEX-VERTEX-FETCH-GUARD",
                       "++stats_.failed;", "return false;"):
            self.assertIn(marker, patched)
        with self.assertRaises(ValueError):
            patch(TEXTURE_NEW + VERTEX_OLD)
        with self.assertRaises(ValueError):
            patch("unknown upstream source")

    def test_lod_capture_is_provenance_only_and_rate_limited(self):
        self.assertIn("n % 500000u == 0", PROBE_NEW)
        self.assertIn("reported < 8", PROBE_NEW)
        self.assertIn("native_scale={:.3f},{:.3f}", PROBE_NEW)
        self.assertIn("lod_bias", PROBE_NEW)
        self.assertIn("regs[first + 5u]", PROBE_NEW)
        self.assertIn("false", PROBE_CVAR_NEW)
        self.assertNotIn("desc.MipLODBias =", PROBE_NEW)
        self.assertNotIn("fetch.dword_0 =", PROBE_NEW)
        self.assertNotIn("RequestTextures", PROBE_NEW)

    def test_log185_invalid_texture_fetch_types_are_not_textures(self):
        # First dwords of malformed descriptors from user log 185.
        bad = (0x44A00000, 0x00010A2F, 0x00000A31, 0x00000003,
               0x028063AB, 0x00000000)
        self.assertTrue(all((word & 3) != 2 for word in bad))
        self.assertEqual(0x00000002 & 3, 2)
        self.assertIn("GetUsedTextureMaskAfterTranslation", TEXTURE_NEW)
        self.assertIn("fetch.type != xenos::FetchConstantType::kTexture", TEXTURE_NEW)
        self.assertIn("fetch.size_1d.width >= 16384u", TEXTURE_NEW)
        self.assertNotIn("gpu_allow_invalid_fetch_constants", TEXTURE_NEW)

    def test_vertex_buffer_physical_bounds_and_64bit_arithmetic(self):
        limit = 0x20000000
        def valid(kind, address_words, size_words):
            begin = address_words * 4
            size = size_words * 4
            return kind == 3 and size > 0 and begin < limit and size <= limit - begin
        self.assertTrue(valid(3, 0x04000000, 1024))
        self.assertFalse(valid(0, 0x04000000, 1024))
        self.assertFalse(valid(3, 0, 0))
        self.assertFalse(valid(3, 0x08000000, 1))
        self.assertFalse(valid(3, 0x07FFFFFF, 2))
        self.assertIn("uint64_t(fetch.address) * 4ull", VERTEX_NEW)
        self.assertIn("vertex_bytes > 0x20000000ull - vertex_start", VERTEX_NEW)
        self.assertIn("uint32_t(fetch.address), uint32_t(fetch.size)", VERTEX_NEW)
        self.assertNotIn("memory_->RequestRange(fetch.address << 2", VERTEX_NEW)

if __name__ == "__main__":
    unittest.main()
