#!/usr/bin/env python3
"""Source-only guards for the actual NativeX texture identity bridge."""
import unittest
from patch_native_x_hulk_live_texture_fingerprint import (
    patch,INCLUDES_OLD,BODY_OLD,CVAR_ANCHOR,CVAR,BODY_NEW)

class LiveOriginalGpuTextureIdentityTest(unittest.TestCase):
    def test_real_guest_upload_anchor_and_idempotence(self):
        original=INCLUDES_OLD+"\n"+CVAR_ANCHOR+"\n"+BODY_OLD+"\n"
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
                      "hulk_fingerprint(hulk_second_at)"):
            self.assertIn(token,updated)
        self.assertIn("false,",CVAR)
        self.assertNotIn("CreateTexture(",BODY_NEW)
        self.assertNotIn("SetTexture",BODY_NEW)
        self.assertNotIn("RequestTextures",BODY_NEW)
        self.assertNotIn("return false",BODY_NEW)
        self.assertIn("base_hash = hash(key.base_page, hulk_base_bytes);",BODY_NEW)
        with self.assertRaises(ValueError):
            patch("unknown NativeX source")
        with self.assertRaises(ValueError):
            patch(updated+BODY_OLD)
        with self.assertRaises(ValueError):
            patch(original+BODY_OLD)

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
