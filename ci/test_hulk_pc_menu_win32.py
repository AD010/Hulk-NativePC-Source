#!/usr/bin/env python3
"""Regression: handle F10 Win32 system-key and log observer hotkey."""
from pathlib import Path
import unittest
R = Path(__file__).resolve().parents[1]

class TestWin32Menu(unittest.TestCase):
    def test_f10_system_key_reaches_pc_menu(self):
        win=(R/"native_port/src/launcher/game_window.cpp").read_text()
        self.assertIn("msg == WM_SYSKEYDOWN", win)
        self.assertIn("msg == WM_KEYDOWN", win)
        self.assertIn("msg == WM_SYSKEYUP", win)
        self.assertIn("wp == VK_F10", win)
        self.assertIn("[HULK/PCMENU] F10 message=",win)
        self.assertIn("PcMenuToggle();",win)
        self.assertIn("NativeInputReleaseCapture();",win)
        self.assertIn("wp == VK_F11",win)
        ui=(R/"native_port/src/pc_menu.cpp").read_text()
        self.assertIn("visible.exchange(open",ui)
        self.assertIn("RequestPaint();",ui)
        self.assertIn('ImGui::Button("Exit to Windows"',ui)
        app=(R/"native_port/src/incredible_hulk_app.h").read_text()
        self.assertIn("NativePC UI registered; F10",app)
        self.assertIn("window()->RequestClose();",app)
    def test_observer_inactive_message(self):
        source=(R/"native_port/src/native_x_renderer_hulk.cpp").read_text()
        self.assertIn("F11 inactive unless",source)

if __name__=="__main__":
    unittest.main()
