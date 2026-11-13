#!/usr/bin/env python3
"""Regression: NativePC F10 overlay offers working PC binds and Exit, not a mock button."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
class TestHulkPcMenu(unittest.TestCase):
    def test_overlay_and_close(self):
        app = (ROOT/"native_port/src/incredible_hulk_app.h").read_text()
        menu = (ROOT/"native_port/src/pc_menu.cpp").read_text()
        window = (ROOT/"native_port/src/launcher/game_window.cpp").read_text()
        cmake = (ROOT/"native_port/CMakeLists.txt").read_text()
        self.assertIn("void OnCreateDialogs", app)
        self.assertIn("window()->RequestClose()", app)
        self.assertIn("pc_menu_.reset()", app)
        self.assertIn("VK_F10", window)
        self.assertIn("NativeInputReleaseCapture()", window)
        inputcode = (ROOT/"native_port/src/launcher/native_input_win.cpp").read_text()
        self.assertIn("if (focused && !PcMenuIsOpen())", inputcode)
        self.assertIn("gpu_guest_fps_limit", menu)
        self.assertIn("HULK/PC-GRAPHICS", menu)
        self.assertIn('SetFlagByName("native_x_renderer_resolution_scale", "2.0")', menu)
        self.assertIn('SetFlagByName("native_x_renderer_scaling", "fsr")', menu)
        self.assertIn('SetFlagByName("native_x_renderer_scaling", "nis")', menu)
        self.assertIn('SetFlagByName("native_x_renderer_sharpness"', menu)
        self.assertIn("DLSS/DLAA requires verified scene", menu)
        config = (ROOT/"native_port/incredible_hulk.toml").read_text()
        self.assertIn('native_x_renderer_scaling = "bilinear"', config)
        self.assertIn("native_x_renderer_sharpness = 0.0", config)

        self.assertIn('SetFlagByName("gpu_guest_fps_limit", "60")', menu)
        # The actual Xbox guest FPS limit, not a fake on-screen selection.
        for value in ("90", "120", "144", "0"):
            self.assertIn('SetFlagByName("gpu_guest_fps_limit", "'+value+'")', menu)
        self.assertIn('SetFlagByName("vsync", vsync ? "true" : "false")', menu)
        self.assertIn("HULK/FPS-CHOICES", menu)
        self.assertIn("Selecting a higher FPS does not change the physics timestep", menu)
        self.assertIn("gpu_guest_fps_limit = 0", config)
        self.assertIn("src/pc_menu.cpp", cmake)
        self.assertIn('ImGui::Button("Exit to Windows"', menu)
        self.assertIn('ImGui::Button("Resume game"', menu)
        self.assertIn("PcMenuSetOpen(false)", menu)
        for name in ("hulk_bind_forward","hulk_bind_confirm",
                     "hulk_bind_primary","hulk_bind_back"):
            self.assertIn('Bind("'+name+'")',menu)

if __name__=="__main__":
    unittest.main()
