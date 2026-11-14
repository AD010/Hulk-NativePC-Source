#!/usr/bin/env python3
"""Regression tests for invalid texture/vertex fetches in Hulk log 185."""
import unittest
from patch_native_x_hulk_draw_fetch_guard import (
    patch, TEXTURE_OLD, TEXTURE_NEW, VERTEX_OLD, VERTEX_NEW,
    PROBE_NEW, PROBE_CVAR_ANCHOR, PROBE_CVAR_NEW)

class HulkDrawFetchGuardTests(unittest.TestCase):
    def test_atomic_and_idempotent(self):
        original = ("#include <bit>\n" + TEXTURE_OLD + "\n" + VERTEX_OLD +
                    "\n" + PROBE_CVAR_ANCHOR)
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

    def test_probe_uses_all_pin_compatible_anchors(self):
        self.assertIn("native_x_renderer_draw_log", PROBE_CVAR_ANCHOR)
        self.assertNotIn("native_x_renderer_scale_in_constants", PROBE_CVAR_ANCHOR)
        self.assertIn("guest_textures_->RequestTextures(used_texture_mask);", TEXTURE_NEW)
        self.assertIn("HULK/MIP-PROVENANCE", TEXTURE_NEW)
        self.assertIn('rex::cvar::Query<int32_t>("anisotropic_override")', PROBE_NEW)
        self.assertNotIn("textures_->AnisotropicOverride()", PROBE_NEW)
        self.assertNotIn("const float lod_bias =", PROBE_NEW)

    def test_rejected_shader_used_fetch_has_real_vs_ps_provenance(self):
        # Read-only sampling: no guessing an XDK shadow slot or changing
        # original register values to silence colored building flashes.
        for source in (TEXTURE_NEW, VERTEX_NEW):
            self.assertIn("HULK/FETCH-SHADER-OWNER-203", source)
            self.assertIn("vertex_shader->ucode_data_hash()", source)
            self.assertIn("n <= 20 || (n % 10000) == 0", source)
            self.assertIn("++stats_.failed;", source)
            self.assertIn("return false;", source)
        self.assertIn("pixel_shader ? pixel_shader->ucode_data_hash() : 0ull",
                      TEXTURE_NEW)
        self.assertIn("VS={:016X}", TEXTURE_NEW)
        self.assertIn("PS={:016X}", TEXTURE_NEW)
        self.assertIn("VS={:016X}", VERTEX_NEW)
        self.assertNotIn("SetVertexFetch", VERTEX_NEW)
        self.assertNotIn("SetTextureFetch", TEXTURE_NEW)

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
