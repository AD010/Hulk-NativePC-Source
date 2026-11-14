#!/usr/bin/env python3
"""Gate USA region, baseline guest pacing and Vulkan FIFO defaults."""
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]

class TestHulkUsGpuDefaults(unittest.TestCase):
    def test_us_region_vsync_and_baseline_guest_pacing(self):
        config = tomllib.loads((ROOT / "native_port/incredible_hulk.toml").read_text(encoding="utf-8"))
        self.assertEqual(config["user_country"], 103)
        self.assertEqual(config["user_language"], 1)
        # Zero leaves the original game pacing intact; it is not a physics unlock.
        self.assertEqual(config["hulk_guest_swap_fps_cap"], 0)
        self.assertNotIn("gpu_guest_fps_limit", config)
        sdk_patch = (ROOT / "ci/patch_sdk_hulk_fps_probe.py").read_text()
        self.assertNotIn('REXCVAR_DEFINE_INT32(hulk_guest_swap_fps_cap', sdk_patch)
        self.assertIn('rex::cvar::Query<int32_t>("hulk_guest_swap_fps_cap")', sdk_patch)
        # Only the native application owns the register call.
        app = (ROOT / "native_port/src/native_x_renderer_hulk.cpp").read_text()
        self.assertEqual(app.count('REXCVAR_DEFINE_INT32(hulk_guest_swap_fps_cap,'), 1)
        self.assertEqual(config["vsync"], True)
        for key in ("vulkan_allow_present_mode_immediate",
                    "vulkan_allow_present_mode_mailbox",
                    "vulkan_allow_present_mode_fifo_relaxed"):
            self.assertIs(config[key], False)
        self.assertFalse(config.get("hulk_gpu_hook_probe", False))
        self.assertTrue(config["native_x_renderer"])
        self.assertTrue(config["native_x_renderer_present_with_swaps"])
        self.assertTrue(config["native_x_renderer_thread"])
        # Revert unbenchmarked 512 look-ahead; use D140's pinned 256
        # until a matched-case Vulkan benchmark proves a better window.
        self.assertEqual(config["native_x_renderer_upload_ahead"], 256)
        # 2x native geometry and valid Xbox mip chains are separate.
        # Xenos D44 AF override 4 is 8x on eligible samplers, never new
        # texture pixels. The lightweight mip audit is temporary for QA.
        self.assertEqual(config["native_x_renderer_resolution_scale"], 2.0)
        self.assertEqual(config["anisotropic_override"], 4)
        self.assertFalse(config["native_x_renderer_hulk_mip_probe"])
        # User log203 proved that an older TOML or saved CVar can override
        # packaged scaling/AF/sharpness settings. Normalize unsupported
        # scaling names before NativeX reads its startup-only values.
        self.assertEqual(config["native_x_renderer_scaling"], "bilinear")
        self.assertEqual(config["native_x_renderer_sharpness"], 0.0)
        self.assertIn('requested_scaling != "bilinear"', app)
        self.assertIn('requested_scaling != "fsr"', app)
        self.assertIn('requested_scaling != "nis"', app)
        self.assertIn('rex::cvar::SetFlagByName("native_x_renderer_scaling", "bilinear")', app)
        self.assertIn("[HULK/QUALITY-203] effective scaling=", app)
        self.assertIn('rex::cvar::Query<int32_t>("native_x_renderer_upload_ahead")', app)
        self.assertLess(app.index("HULK/EFFECTIVE-QUALITY-203"),
                        app.index('native_x_renderer::Initialize('))
        fetch_patch = (ROOT / "ci/patch_native_x_hulk_draw_fetch_guard.py").read_text()
        self.assertIn("HULK/MIP-PROVENANCE", fetch_patch)
        self.assertIn("native_x_renderer_hulk_mip_probe", fetch_patch)
        import configparser
        scene_profile = configparser.ConfigParser(interpolation=None)
        scene_profile.read(ROOT / "native_port/hulk_native_x_profile.ini")
        self.assertEqual(int(scene_profile["profile"]["scene_resolve_caller"], 0),
                         0x8263BF50)
        source = (ROOT / "native_port/src/native_x_hulk_profile.cpp").read_text()
        self.assertIn("HULK/SCENE-RESOLVE-CALLER-6534", source)
        self.assertIn("v!=0x8263BF50u", source)
        self.assertFalse(config["hulk_world_stream_trace"])
        self.assertFalse(config["hulk_native_x_verify_only"])
        # Source verification and trace are developer-only: they execute
        # extra per-frame GPU/CPU observer work in the user's retail build.
        self.assertFalse(config["native_x_renderer_verify"])
        self.assertFalse(config["native_x_renderer_trace"])
        self.assertFalse(config["native_x_renderer_lean_setters"])
        # Existing installations may retain a TOML enabling the D140 fast
        # path. Force full capture before Initialize computes g_lean.
        override = 'rex::cvar::SetFlagByName("native_x_renderer_lean_setters", "false")'
        self.assertIn(override, app)
        self.assertLess(app.index(override), app.index('native_x_renderer::Initialize('))
        # The D140 fast path must not survive a stale user TOML or failed
        # runtime override. A verified false value is a startup invariant.
        self.assertIn('if (rex::cvar::Query<bool>("native_x_renderer_lean_setters")) {', app)
        self.assertIn("Unsafe D140 lean setters remain enabled", app)
        self.assertIn("refusing native renderer initialization", app)
        self.assertIn("Full setter PM4 gap capture enforced (lean OFF)", app)
        self.assertLess(app.index("Unsafe D140 lean setters remain enabled"),
                        app.index('native_x_renderer::Initialize('))
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
