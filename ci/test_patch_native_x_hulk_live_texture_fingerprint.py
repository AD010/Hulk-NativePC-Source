#!/usr/bin/env python3
"""Source-only guards for the actual NativeX texture identity bridge."""
import unittest
from patch_native_x_hulk_live_texture_fingerprint import (
    patch,patch_draw,INCLUDES_OLD,BODY_OLD,CVAR_ANCHOR,CVAR,BODY_NEW,
    DRAW_INCLUDES_OLD,DRAW_LOGGING_OLD,DRAW_CVAR_OLD,DRAW_BIND_OLD,
    DRAW_BIND_NEW,GUEST_VIEW_OLD,GUEST_VIEW_NEW,MIPS_OLD,MIPS_NEW)

class LiveOriginalGpuTextureIdentityTest(unittest.TestCase):
    def test_real_guest_upload_anchor_and_idempotence(self):
        original=INCLUDES_OLD+"\n"+CVAR_ANCHOR+"\n"+BODY_OLD+"\n"+MIPS_OLD+"\n"+GUEST_VIEW_OLD+"\n"
        updated,changed=patch(original)
        self.assertTrue(changed)
        self.assertEqual(patch(updated),(updated,False))
        for token in ("HULK/LIVE-XENOS-ARC-IDENTITY",
                      "host_texture.GetGuestBaseSize()",
                      "GetWidth()", "GetHeight()", "key.format",
                      "hulk_base_bytes >= 128",
                      "hulk_base_bytes >= 4224 ? 4096",
                      "UINT64_C(14695981039346656037)",
                      "UINT64_C(1099511628211)",
                      "native_x_renderer_hulk_live_texture_id",
                      "hulk_fingerprint(0)",
                      "hulk_fingerprint(hulk_second_at)",
                      "HULK/GUEST-SHADER-IDENTITY",
                      "texture->base_hash.has_value()",
                      "texture->key()",
                      "base_xxh3={:016x}",
                      "packed_mips_xxh3={:016x}",
                      "HULK/LIVE-XENOS-MIP-IDENTITY",
                      "per-level ARC identity unproven",
                      "not submitted Draw proof"):
            self.assertIn(token,updated)
        self.assertIn("false,",CVAR)
        self.assertNotIn("CreateTexture(",BODY_NEW)
        self.assertNotIn("CreateTexture(",MIPS_NEW)
        self.assertIn("mips_hash = hash(key.mip_page, hulk_mips_bytes);",MIPS_NEW)
        self.assertIn("hulk_mips_bytes >= 128",MIPS_NEW)
        self.assertNotIn("return false;",MIPS_NEW)
        self.assertNotIn("CreateTexture(",GUEST_VIEW_NEW)
        self.assertNotIn("return nullptr",GUEST_VIEW_NEW)
        self.assertNotIn("variable->Set(",GUEST_VIEW_NEW)
        self.assertIn("texture->MarkAsUsed();",GUEST_VIEW_NEW)
        self.assertNotIn("SetTexture",BODY_NEW)
        self.assertNotIn("RequestTextures",BODY_NEW)
        self.assertNotIn("return false",BODY_NEW)
        self.assertIn("base_hash = hash(key.base_page, hulk_base_bytes);",BODY_NEW)
        self.assertIn("full_xxh3={:016x}",BODY_NEW)
        self.assertIn("hulk_base_bytes, base_hash, hulk_fingerprint(0)",BODY_NEW)
        with self.assertRaises(ValueError):
            patch("unknown NativeX source")
        with self.assertRaises(ValueError):
            patch(updated+BODY_OLD)
        with self.assertRaises(ValueError):
            patch(updated+GUEST_VIEW_OLD)
        with self.assertRaises(ValueError):
            patch(updated+MIPS_OLD)
        with self.assertRaises(ValueError):
            patch(original+BODY_OLD)

    def test_shader_view_attestation_and_fail_closed_binding(self):
        # D140 Textures::View returns actual cached shader-view selection.
        original=(DRAW_LOGGING_OLD+"\n"+DRAW_INCLUDES_OLD+"\n"+
                  DRAW_CVAR_OLD+"\n"+DRAW_BIND_OLD+"\n")
        changed, installed = patch_draw(original)
        self.assertTrue(installed)
        self.assertEqual(patch_draw(changed), (changed, False))
        self.assertIn("HULK/ACTIVE-SHADER-RESOURCE", changed)
        self.assertIn("native_x_renderer_hulk_live_texture_id", changed)
        self.assertIn("hulk_kind = std::min<uint32_t>(kept.value.kind, 3u)", changed)
        self.assertIn("fetch.base_address", changed)
        self.assertIn("fetch_constant", changed)
        self.assertIn("uint32_t(fetch.format)", changed)
        self.assertIn("kind={} count={}", changed)
        self.assertIn("return kept.value.view;", changed)
        self.assertIn("Count(kept.value.kind);", changed)
        self.assertIn("no Draw submit claim", changed)
        # No source image/host memory modification by the observer.
        self.assertNotIn("CreateTexture(", DRAW_BIND_NEW)
        self.assertNotIn("variable->Set(", DRAW_BIND_NEW)
        self.assertNotIn("RequestTextures(", DRAW_BIND_NEW)
        self.assertNotIn("return nullptr", DRAW_BIND_NEW)
        with self.assertRaises(ValueError):
            patch_draw("unknown renderer source")
        with self.assertRaises(ValueError):
            patch_draw(changed + DRAW_BIND_OLD)
        with self.assertRaises(ValueError):
            patch_draw(changed.replace("native_x_renderer_hulk_live_texture_id",
                                        "missing_cvar", 1))
 
    def test_two_witnesses_separate_original_content(self):
        def fnv(b):
            n=14695981039346656037
            for c in b:
                n=((n^c)*1099511628211) & ((1<<64)-1)
            return n
        a=bytes((i%256 for i in range(8192)))
        b=bytearray(a)
        b[4200]^=1
        self.assertEqual(fnv(a[:128]),fnv(b[:128]))
        self.assertNotEqual(fnv(a[4096:4224]),fnv(b[4096:4224]))
        self.assertNotEqual(fnv(bytes(128)),fnv(bytes([1])+bytes(127)))

if __name__=="__main__":
    unittest.main()
