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
        self.assertIn("[HULK/PCMENU] shortcut={} message={} open={}",win)
        self.assertIn("PcMenuToggle();",win)
        self.assertIn("NativeInputReleaseCapture();",win)
        self.assertIn("wp == VK_F11",win)
        self.assertIn("wp == VK_ESCAPE",win)
        self.assertIn("if (close_with_escape) NativeInputConsumeEscape(true);",win)
        self.assertIn("NativeInputEscapeConsumed()",win)
        self.assertIn("msg == WM_KEYDOWN && wp == VK_ESCAPE",win)
        self.assertIn("!(lp & (1L << 29))",win)
        inputcode=(R/"native_port/src/launcher/native_input_win.cpp").read_text()
        guard=inputcode.split("bool NativeInputUsesEscape() {",1)[1]
        guard=guard.split("\nnamespace {",1)[0]
        for name in ("hulk_bind_forward", "hulk_bind_backward", "hulk_bind_left", "hulk_bind_right", "hulk_bind_menu_up", "hulk_bind_menu_down", "hulk_bind_menu_left",
                     "hulk_bind_menu_right", "hulk_bind_confirm", "hulk_bind_back", "hulk_bind_primary", "hulk_bind_secondary", "hulk_bind_alt_back", "hulk_bind_lb",
                     "hulk_bind_rb", "hulk_bind_lt", "hulk_bind_rt", "hulk_bind_start", "hulk_bind_select", "hulk_bind_rstick_click", "hulk_bind_walk"):
            self.assertIn("REXCVAR_GET("+name+")",guard)
        self.assertIn("options::AnyBindingKey(names, options::IsEscapeKey)",guard)
        self.assertIn('BindTokenDown("Esc") && !NativeInputUsesEscape()',inputcode)
        self.assertIn("!g_host_escape && !Down(VK_MENU) && Down(VK_ESCAPE)",inputcode)
        launcher=(R/"native_port/src/launcher/return_to_launcher.cpp").read_text()
        self.assertIn("key.Seconds(!NativeInputEnabled() && EscHeld())",launcher)
        self.assertIn("F10: PC settings", (R/"native_port/src/pc_menu.cpp").read_text())
        ui=(R/"native_port/src/pc_menu.cpp").read_text()
        self.assertIn("visible.exchange(open",ui)
        self.assertIn("RequestPaint();",ui)
        self.assertIn("if (old && !open) GameWindowMenuClosed();", ui)
        self.assertIn("void GameWindowMenuClosed()", win)
        self.assertIn("g_restore_after_focus", win)
        self.assertIn("g_restore_after_menu", win)
        self.assertIn("msg == WM_ACTIVATEAPP", win)
        self.assertIn("received >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)", win)
        self.assertIn("GetForegroundWindow() == hwnd", win)
        self.assertIn('ImGui::Button("Exit to Windows"',ui)
        app=(R/"native_port/src/incredible_hulk_app.h").read_text()
        self.assertIn("PC settings registered on F10",app)
        self.assertIn("window()->RequestClose();",app)
    def test_observer_inactive_message(self):
        source=(R/"native_port/src/native_x_renderer_hulk.cpp").read_text()
        # A source-only XDK observer may run with Xenos rendering; the
        # old unconditional F11-inactive log no longer describes this path.
        self.assertIn("optional XDK observer disabled",source)
        self.assertIn("const bool eor_observer_only =",source)
        self.assertIn("if (eor_observer_only) {",source)
        self.assertIn("Xenos remains presenter",source)

if __name__=="__main__":
    unittest.main()
