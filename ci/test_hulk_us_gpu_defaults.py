#!/usr/bin/env python3
"""Gate default USA region and 30 FPS Vulkan FIFO against accidental regressions."""
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]

class TestHulkUsGpuDefaults(unittest.TestCase):
    def test_us_region_vsync_and_30_guest_fps(self):
        config = tomllib.loads((ROOT / "native_port/incredible_hulk.toml").read_text(encoding="utf-8"))
        self.assertEqual(config["user_country"], 103)
        self.assertEqual(config["user_language"], 1)
        self.assertEqual(config["gpu_guest_fps_limit"], 30)
        self.assertEqual(config["vsync"], True)
        for key in ("vulkan_allow_present_mode_immediate",
                    "vulkan_allow_present_mode_mailbox",
                    "vulkan_allow_present_mode_fifo_relaxed"):
            self.assertIs(config[key], False)
        self.assertFalse(config.get("hulk_gpu_hook_probe", False))
        self.assertFalse(config["native_x_renderer"])
        self.assertTrue(config["native_x_renderer_present_with_swaps"])
        self.assertTrue(config["native_x_renderer_thread"])
        self.assertFalse(config["hulk_world_stream_trace"])
        self.assertFalse(config["hulk_native_x_verify_only"])
        self.assertTrue(config["native_x_renderer_verify"])
        self.assertTrue(config["native_x_renderer_trace"])
        self.assertEqual(config["native_x_renderer_verify_at"], 900)
        self.assertEqual(config["native_x_renderer_trace_dump_at"], 900)
        self.assertEqual(config["bind_native_x_renderer_verify"], "F11")
        module = (ROOT / "native_port/src/native_x_renderer_hulk.cpp").read_text()
        self.assertIn('if (verify_only && !native_x_renderer::Renders())', module)
        self.assertIn("[HULK/VERIFY] Xenos visible", module)
    def test_asset_root_rejects_pal_xex(self):
        source = (ROOT / "native_port/src/assets_locator.cpp").read_text(encoding="utf-8")
        self.assertIn("346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0", source)
        self.assertIn("IsUsXex(xex)", source)
        self.assertIn("BCRYPT_SHA256_ALGORITHM", source)
        self.assertIn("ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d", source)
        self.assertIn("46e6c6ec9f7428ba3a1737ab7ebad564c1b027b006c3e35156d2e036ab8b2820", source)
        self.assertIn("AuditUsIndex(candidate)", source)
        cmake = (ROOT / "native_port/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("comdlg32 bcrypt", cmake)

if __name__ == "__main__":
    unittest.main()
