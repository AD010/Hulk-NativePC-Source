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
        self.assertIn("src/pc_menu.cpp", cmake)
        self.assertIn('ImGui::Button("Exit to Windows"', menu)
        self.assertIn('ImGui::Button("Resume game"', menu)
        self.assertIn("PcMenuSetOpen(false)", menu)
        for name in ("hulk_bind_forward","hulk_bind_confirm",
                     "hulk_bind_primary","hulk_bind_back"):
            self.assertIn('Bind("'+name+'")',menu)

if __name__=="__main__":
    unittest.main()
