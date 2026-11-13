#!/usr/bin/env python3
"""Regression checks for native Vulkan pre-HUD AA and PM4 topology guard."""
import unittest
from patch_native_x_hulk_scene_aa import PATCHES as AA, transform as aa_transform
from patch_native_x_hulk_geometry_guard import PATCHES as GEOM, transform as geom_transform

class HulkNativeAAAndGeometryGuard(unittest.TestCase):
    def test_exact_patch_and_idempotence(self):
        for patch, transform in [(AA, aa_transform), (GEOM, geom_transform)]:
            for path, rules in patch.items():
                with self.subTest(path=path):
                    source = "\n".join(old for old, new in rules)
                    output, changed = transform(source, rules)
                    self.assertTrue(changed)
                    self.assertEqual(transform(output, rules), (output, False))
                    with self.assertRaises(ValueError):
                        transform("unknown upstream", rules)

    def test_no_fullscreen_after_hud_smoothing(self):
        cpp = "\n".join(new for old, new in AA["src/renderer.cpp"])
        self.assertIn("native_x_renderer_hulk_auto_scene_aa, false", cpp)
        self.assertIn("surface.msaa() == 2", cpp)
        self.assertIn("target->guest_width == g_profile->output_width", cpp)
        self.assertIn("!g_scene_antialiased", cpp)
        self.assertIn("[HULK/SCENE-AA]", cpp)
        self.assertNotIn("CapturePresent", cpp)
        self.assertNotIn("ComposePicture", cpp)

    def test_real_host_size_half_guest_scene_aa(self):
        # Log195: sample-grid Resolve 640x360/4x -> 640x360 at host
        # 2560x1440. Previous outer gate demanded 1280x720 and never ran.
        cpp = AA["src/renderer.cpp"][2][1]
        self.assertIn("HULK/SCENE-AA-HALF-RESOLVE", cpp)
        self.assertIn("HULK/SCENE-RESOLVE-CALLER-6534", cpp)
        self.assertIn("c.scene_caller && surface", cpp)
        self.assertIn("target->guest_width * 2 == g_profile->output_width", cpp)
        self.assertIn("source->width == target->width", cpp)
        self.assertIn("target->width == g_scale.HostWidth(g_profile->output_width)", cpp)
        self.assertIn("(c.scene_caller &&", cpp)
        self.assertIn("hulk_scene_resolve) &&", cpp)
        def is_scene(src_msaa, src_depth, src_w, src_h, dst_w, dst_h,
                     src_host, dst_host, slice_number=0, already_aa=False):
            return (src_msaa == 4 and not src_depth and
                    src_w*2 == 1280 and src_h*2 == 720 and
                    src_host == (2560, 1440) and dst_host == (2560, 1440) and
                    ((dst_w,dst_h) in ((640,360),(1280,720))) and
                    slice_number == 0 and not already_aa)
        self.assertTrue(is_scene(4,False,640,360,640,360,
                                 (2560,1440),(2560,1440)))
        self.assertTrue(is_scene(4,False,640,360,1280,720,
                                 (2560,1440),(2560,1440)))
        self.assertFalse(is_scene(4,True,640,360,640,360,
                                  (2560,1440),(2560,1440)))
        self.assertFalse(is_scene(4,False,640,360,640,360,
                                  (1280,720),(2560,1440)))
        self.assertFalse(is_scene(1,False,640,360,640,360,
                                  (2560,1440),(2560,1440)))
        self.assertFalse(is_scene(4,False,640,360,640,360,
                                  (2560,1440),(2560,1440),already_aa=True))
        self.assertFalse(is_scene(4,False,640,360,640,360,
                                  (2560,1440),(2560,1440),slice_number=1))
        self.assertFalse(is_scene(4,False,1280,720,1280,720,
                                  (2560,1440),(2560,1440)))

    def test_reject_synthetic_corrupted_topology_only(self):
        cpp = "\n".join(new for old, new in GEOM["src/renderer.cpp"])
        self.assertIn("LastWrite(rex::graphics::XE_GPU_REG_VGT_DRAW_INITIATOR", cpp)
        self.assertIn("topology == 0 || topology == 48 || topology == 49", cpp)
        self.assertIn("[HULK/GEOMETRY-GUARD]", cpp)
        # OnHulkGapSingleDraw has no namespace rg alias: use an SDK-qualified register.
        self.assertNotIn("LastWrite(rg::", cpp)
        self.assertNotIn("CaptureDraw", cpp)

if __name__ == "__main__":
    unittest.main()
