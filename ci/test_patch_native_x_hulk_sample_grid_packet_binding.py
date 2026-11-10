#!/usr/bin/env python3
"""Source-only regression tests for the exact Hulk XDK/PM4 sample-grid pair."""
import unittest
from patch_native_x_hulk_sample_grid_packet_binding import OLD, NEW, patch_text

class HulkPacketSampleViewTests(unittest.TestCase):
    def test_exact_source_and_idempotency(self):
        source = "bool HulkPacketSurfaces(\n" + OLD
        out, changed = patch_text(source)
        self.assertTrue(changed)
        self.assertEqual(patch_text(out), (out, False))
        with self.assertRaises(ValueError):
            patch_text("bool HulkPacketSurfaces(\nchanged upstream")

    def test_exact_real_view_only(self):
        for expected in (
            "bound.depth == (slot == 4)",
            "bound.width >= 2 && bound.height >= 2",
            "!(bound.width & 1u) && !(bound.height & 1u)",
            "bound.width / 2 <= (gpu_surface & 0x3FFFu)",
            "bound.pitch() == 2u * (gpu_surface & 0x3FFFu)",
            "bound.msaa() == 0",
            "((gpu_surface >> 16) & 3u) == 2u",
            "(gpu_info & 0x000F0FFFu) == (bound.info & 0x000F0FFFu)",
            "packet_view = bound",
            "packet_view.width = bound.width / 2",
            "packet_view.height = bound.height / 2",
            "HulkRememberSurface(packet_view, slot);",
            "[HULK/PM4-HISTORY-ALIAS]",
            "2u * (gpu_surface & 0x3FFFu)",
            "const GuestSurface live = known ? ReadSurface(base, known.object",
            "live.surface_info == known.surface_info && live.info == known.info",
            "live.width == known.width && live.height == known.height",
            "live.depth == (slot == 4)",
            "packet_view = verified",
        ):
            self.assertIn(expected, NEW)
        self.assertIn("[HULK/PM4-SAMPLE-VIEW]", NEW)
        self.assertIn("continue;", NEW)

    def test_general_paired_pitch_and_format(self):
        # The two *real* pairs from user logs; these do NOT invent GPU surfaces.
        def authentic_alias(gpu_surface, gpu_info, xdk_surface, xdk_info, w, h, depth, slot):
            pitch = gpu_surface & 0x3FFF
            return (bool(pitch) and (gpu_surface >> 16) & 3 == 2 and
                    (xdk_surface >> 16) & 3 == 0 and
                    (xdk_surface & 0x3FFF) == 2 * pitch and
                    w >= 2 and h >= 2 and w % 2 == 0 and h % 2 == 0 and
                    w // 2 <= pitch and depth == (slot == 4) and
                    (gpu_info & 0x000F0FFF) == (xdk_info & 0x000F0FFF))
        self.assertTrue(authentic_alias(0x0A020280, 0x400, 0x14000500, 0x400,
                                        1280, 720, False, 0))
        # #48 log 171: genuine 1024x1024 depth, pitch 1040, vs GPU 4x pitch 520.
        self.assertTrue(authentic_alias(0x08020208, 0, 0x10000410, 0,
                                        1024, 1024, True, 4))
        self.assertFalse(authentic_alias(0x08020208, 1, 0x10000410, 0,
                                         1024, 1024, True, 4))
        self.assertFalse(authentic_alias(0x08020208, 0, 0x10000411, 0,
                                         1024, 1024, True, 4))
        self.assertFalse(authentic_alias(0x08020208, 0, 0x10000410, 0,
                                         1024, 1024, False, 4))
        self.assertFalse(authentic_alias(0x08010208, 0, 0x10000410, 0,
                                         1024, 1024, True, 4))

    def test_real_log173_rt0_edram_rebase(self):
        # A genuine 1280x720/1x XDK RT0 may be stale in EDRAM *base*,
        # not in dimensions or Xenos color format. We must preserve the
        # actual packet's GPU base=0 instead of silently binding base=1024.
        self.assertIn("HULK/PM4-RT0-REBASE", NEW)
        self.assertIn("gpu_surface == 0x0A020280u && gpu_info == 0u", NEW)
        self.assertIn("bound.info == 0x400u", NEW)
        self.assertIn("packet_view.info = gpu_info", NEW)
        self.assertIn("packet_view.surface_info = gpu_surface", NEW)
        self.assertIn("bound.pitch() == bound.width", NEW)
        def accepts(surface, info, bound_surface, bound_info, width, height, msaa, depth=False):
            return (not depth and surface == 0x0A020280 and info == 0 and
                    bound_info == 0x400 and
                    ((bound_surface == 0x14000500 and width == 1280 and height == 720 and msaa == 0) or
                     (bound_surface == surface and width == 640 and height == 360 and msaa == 2)))
        self.assertTrue(accepts(0x0A020280, 0, 0x14000500, 0x400, 1280, 720, 0))
        self.assertTrue(accepts(0x0A020280, 0, 0x0A020280, 0x400, 640, 360, 2))
        self.assertFalse(accepts(0x0A020280, 0x400, 0x14000500, 0x400, 1280, 720, 0))
        self.assertFalse(accepts(0x0A020280, 0, 0x14000500, 0x401, 1280, 720, 0))
        self.assertFalse(accepts(0x0A020280, 0, 0x14000500, 0x400, 1280, 720, 0, True))

    def test_real_log175_rt0_1x_edram_rebase(self):
        # Confirmed build 56: packet RB_SURFACE_INFO=0x14000500 / RB_COLOR_INFO=0
        # and genuine XDK-bound RT0 RB_SURFACE_INFO=0x14000500 / INFO=0x400.
        # Preserve format, pitch, dimensions and sample count. Never accept depth.
        self.assertIn("[HULK/PM4-RT0-1X-REBASE]", NEW)
        self.assertIn("gpu_surface == 0x14000500u && gpu_info == 0u", NEW)
        self.assertIn("bound.surface_info == 0x14000500u && bound.info == 0x400u", NEW)
        self.assertIn("bound.pitch() == 1280u && bound.msaa() == 0u", NEW)
        self.assertIn("packet_view.info = gpu_info", NEW)
        self.assertIn("HulkRememberSurface(packet_view, slot);", NEW)
        def accepts(slot, depth, gpu_surface, gpu_info, xdk_surface, xdk_info,
                    width, height, pitch, msaa):
            return (slot == 0 and not depth and gpu_surface == 0x14000500 and
                    gpu_info == 0 and xdk_surface == 0x14000500 and
                    xdk_info == 0x400 and width == 1280 and height == 720 and
                    pitch == 1280 and msaa == 0)
        args = (0, False, 0x14000500, 0, 0x14000500, 0x400, 1280, 720, 1280, 0)
        self.assertTrue(accepts(*args))
        for i, invalid in ((0, 4), (1, True), (2, 0x0A020280),
                           (3, 0x401), (4, 0x14000400), (5, 0),
                           (6, 640), (7, 360), (8, 640), (9, 2)):
            bad = list(args)
            bad[i] = invalid
            self.assertFalse(accepts(*bad), msg=f"Guard missed changed field {i}")

    def test_log175_unbound_depth_rebase_live_xdk_only(self):
        # A real 1x depth XDK surface has previously been observed in the
        # 4x history. Do not fabricate a surface or misbind a color target.
        self.assertIn("HULK/PM4-DEPTH-REBASE-4X", NEW)
        self.assertIn("slot == 4 && !bound && gpu_surface == 0x0A020280u", NEW)
        self.assertIn("gpu_info == 0x400u", NEW)
        self.assertIn("HulkSurfaceKey(gpu_surface, 0u)", NEW)
        self.assertIn("ReadSurface(base, known.object, true, g_profile->objects)", NEW)
        self.assertIn("known.width == 640 && known.height == 360", NEW)
        self.assertIn("live.width == 1280 && live.height == 720", NEW)
        self.assertIn("known.msaa() == 2", NEW)
        self.assertIn("live.pitch() == 1280 && live.msaa() == 0", NEW)
        self.assertIn("packet_view.info = gpu_info", NEW)
        self.assertIn("HulkRememberSurface(packet_view, slot);", NEW)
        def supported(slot, bound, gpu_surface, gpu_info, known_surface, known_info,
                      known_width, known_height, known_pitch, known_msaa,
                      known_depth, live_surface, live_info, live_width,
                      live_height, live_pitch, live_msaa, live_depth):
            return (slot == 4 and not bound and gpu_surface == 0x0A020280 and
                    gpu_info == 0x400 and known_surface == gpu_surface and
                    known_info == 0 and known_width == 640 and
                    known_height == 360 and known_pitch == 640 and
                    known_msaa == 2 and known_depth and
                    (live_surface & 0x3FFF) == 1280 and live_info == 0 and
                    live_width == 1280 and live_height == 720 and
                    live_pitch == 1280 and live_msaa == 0 and live_depth)
        valid = [4, False, 0x0A020280, 0x400, 0x0A020280, 0,
                 640, 360, 640, 2, True, 0x14000500, 0, 1280,
                 720, 1280, 0, True]
        self.assertTrue(supported(*valid))
        wrong_values = {0: 0, 1: True, 2: 0x14000500, 3: 0,
                        4: 0x14000500, 5: 0x400, 6: 1280, 7: 720,
                        8: 1280, 9: 0, 10: False, 11: 0x14000410,
                        12: 0x400, 13: 640, 14: 360, 15: 640,
                        16: 2, 17: False}
        for index, bad in wrong_values.items():
            changed = valid.copy()
            changed[index] = bad
            self.assertFalse(supported(*changed),
                             msg=f"Unsafe depth descriptor change {index}")

    def test_logged_surface_identity(self):
        gpu_surface = 0x0A020280
        bound_surface = 0x14000500
        gpu_info = bound_info = 0x400
        self.assertEqual(gpu_surface & 0x3FFF, 640)
        self.assertEqual((gpu_surface >> 16) & 3, 2)
        self.assertEqual(bound_surface & 0x3FFF, 1280)
        self.assertEqual((bound_surface >> 16) & 3, 0)
        self.assertEqual(gpu_info & 0x000F0FFF, bound_info & 0x000F0FFF)
        self.assertNotEqual(gpu_surface & 0x0003FFFF, bound_surface & 0x0003FFFF)
        self.assertNotEqual(0x401 & 0x000F0FFF, bound_info & 0x000F0FFF)

if __name__ == "__main__":
    unittest.main()
