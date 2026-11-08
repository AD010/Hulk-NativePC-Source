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
    def test_asset_root_rejects_pal_xex(self):
        source = (ROOT / "native_port/src/assets_locator.cpp").read_text(encoding="utf-8")
        self.assertIn("346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0", source)
        self.assertIn("IsUsXex(xex)", source)
        self.assertIn("BCRYPT_SHA256_ALGORITHM", source)
        cmake = (ROOT / "native_port/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("comdlg32 bcrypt", cmake)

if __name__ == "__main__":
    unittest.main()
