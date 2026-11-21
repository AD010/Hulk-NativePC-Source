#!/usr/bin/env python3
"""Regression: NativePC F10 overlay offers working PC binds and Exit, not a mock button."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
class TestHulkPcMenu(unittest.TestCase):
    def test_nativepc_sections_are_functional_and_no_mock_controls(self):
        # Present real, owner-controlled PC CVars rather than decorative
        # sliders disconnected from the XDK guest or audio engine.
        menu = (ROOT/"native_port/src/pc_menu.cpp").read_text()
        for section in ("Display", "Graphics", "Post-processing",
                        "Audio", "Controls", "Accessibility", "Enhancements"):
            self.assertIn('"' + section + '"', menu)
        self.assertIn("HULK/PC-MENU-SECTIONS", menu)
        self.assertIn("ImGui::Selectable(kSections[i], active_section_ == i)", menu)
        self.assertIn("ImGui::BeginChild(\"##hulk_sections\"", menu)
        self.assertIn("ImGui::BeginChild(\"##hulk_settings\"", menu)
        self.assertEqual(menu.count("ImGui::EndChild();"), 2)
        self.assertIn("ImGui::PopStyleColor(6);", menu)
        self.assertIn("ImGuiWindowFlags_NoTitleBar", menu)
        self.assertIn("ImGuiWindowFlags_NoSavedSettings", menu)
        self.assertIn("art->AddRectFilledMultiColor(", menu)
        self.assertIn("case -1:", menu)
        self.assertIn('ImGui::Selectable("HOME",active_section_==-1)', menu)
        self.assertIn('ImGui::Button("DISPLAY & GRAPHICS"', menu)
        self.assertIn('ImGui::Button("KEYBOARD & MOUSE"', menu)
        self.assertIn('ImGui::OpenPopup("Exit to desktop?")', menu)
        self.assertIn('ImGui::BeginPopupModal("Exit to desktop?"', menu)
        self.assertIn('ImGui::Button("Confirm exit"', menu)
        self.assertIn("int active_section_ = -1;", menu)
        self.assertIn("return_to_home.store(true", menu)
        self.assertIn("Changes saved to incredible_hulk.toml", menu)
        self.assertIn("No inactive sliders or fake apply buttons", menu)
        self.assertIn("No unimplemented accessibility toggles", menu)
        self.assertIn("Native volume ", menu)
        self.assertIn("DrawMouseMultiplier(", menu)
        self.assertIn('Change("hulk_mouse_invert_x"', menu)
        self.assertIn('Change("hulk_mouse_invert_y"', menu)
        self.assertIn("mouse_x_edit_active_", menu)
        self.assertIn("mouse_y_edit_active_", menu)
        self.assertIn("hulk_mouse_horizontal_multiplier", (ROOT/"native_port/src/native_pc_options_store.cpp").read_text())
        self.assertIn("hulk_mouse_vertical_multiplier", (ROOT/"native_port/src/native_pc_options_store.cpp").read_text())
        self.assertIn("f.sensitivity_x", (ROOT/"native_port/src/launcher/native_input_win.cpp").read_text())
        self.assertIn("f.sensitivity_y", (ROOT/"native_port/src/launcher/native_input_win.cpp").read_text())
        self.assertIn("controls are not wired to a verified engine API yet.", menu)
        self.assertIn('Query<bool>("native_x_renderer_pipeline_storage")', menu)
        self.assertIn("GameWindowMenuClosed()", menu)
        # The preview is a themed host overlay, not a false claim that
        # the original Hulk EoR pause-menu resources were altered.
        self.assertIn("not a", menu)
        self.assertIn("Xbox pause-menu", menu)

    def test_native_keyboard_mouse_rebind_options_match_real_input(self):
        menu = (ROOT/"native_port/src/pc_menu.cpp").read_text()
        bindings = (ROOT/"native_port/src/native_pc_bindings.h").read_text()
        input_code = (ROOT/"native_port/src/launcher/native_input_win.cpp").read_text()
        store = (ROOT/"native_port/src/native_pc_options_store.cpp").read_text()
        self.assertIn("HULK/NATIVE-REMAP", menu)
        self.assertIn("ImGui::BeginCombo(label, current.c_str())", menu)
        self.assertIn("Change(key, supported)", menu)
        self.assertIn("options::kBindingTokens", menu)
        # Dragging the startup-only sharpen knob may not rewrite the
        # same TOML dozens of times per second (I/O-driven microstutter).
        self.assertIn("HULK/PC-MENU-NO-DISK-DRAG", menu)
        self.assertIn("ImGui::IsItemDeactivatedAfterEdit()", menu)
        self.assertIn("sharpness_edit_active_", menu)
        self.assertIn("MoveFileExW(temporary.c_str(), config_file.c_str()", store)
        self.assertIn("in.close();", store)
        self.assertNotIn("Full key rebinding from this screen is not implemented yet", menu)
        names = set(re.findall(r'"(hulk_bind_[a-z_]+)"', bindings))
        declared = set(re.findall(r'REXCVAR_DEFINE_STRING\((hulk_bind_[a-z_]+),',
                                  input_code))
        self.assertEqual(names, declared)
        self.assertEqual(len(names), 21)
        for key in names:
            self.assertIn('"' + key + '"', menu)
        self.assertIn("IsBindingOption(key)", store)
        self.assertIn("IsBindingToken(value)", store)
        self.assertIn(
            'key == "native_x_renderer_scaling" || '
            'key == "native_x_renderer_antialiasing" || IsBindingOption(key)',
            store)

    def test_native_host_resolution_aspect_and_safe_guest_mode(self):
        menu = (ROOT/"native_port/src/pc_menu.cpp").read_text()
        config = (ROOT/"native_port/incredible_hulk.toml").read_text()
        gpu = (ROOT/"native_port/src/native_x_renderer_hulk.cpp").read_text()
        store = (ROOT/"native_port/src/native_pc_options_store.cpp").read_text()
        host = (ROOT/"native_port/src/native_resolution.h").read_text()
        test_cpp = (ROOT/"ci/native_pc_options_store_test.cpp").read_text()
        self.assertIn('Change("native_x_renderer_resolution_scale", "0.0")', menu)
        self.assertIn("HULK/NATIVE-AUTO-PIXELS", menu)
        self.assertIn('native_x_renderer_resolution_scale = 0.0', config)
        self.assertIn('native_x_renderer_fill_screen = false', config)
        self.assertIn("HULK/WIDESCREEN-GUARD", gpu)
        self.assertIn("HULK/HOST-PIXELS", menu)
        # HP1 and HP3 recomp menus bound their UI to client size.
        # Hulk must not keep the old fixed 850x610 rectangle at 175% DPI.
        self.assertIn("HULK/PC-MENU-DPI-RESPONSIVE", menu)
        self.assertIn("ImGuiCond_Always", menu)
        self.assertIn("menu_width", menu)
        self.assertIn("menu_height", menu)
        self.assertIn("section_width", menu)
        self.assertIn("footer_height", menu)
        self.assertNotIn("ImGuiCond_FirstUseEver", menu)
        self.assertIn("io.DisplayFramebufferScale.x", menu)
        self.assertIn("io.DisplayFramebufferScale.y", menu)
        self.assertIn("display::FitOriginalPicture(", menu)
        self.assertIn("options::SaveWindowSize(", menu)
        self.assertIn("display::kWindowPresets", menu)
        # The full native PC menu now exposes 17 validated presets,
        # including 2560x1600 (16:10) and ultrawide resolutions.
        self.assertIn("std::array<WindowPreset, 17>", host)
        self.assertIn("std::min(horizontal, vertical)", host)
        self.assertIn("std::max(1.0, fit)", host)
        self.assertIn("SaveWindowSize(", store)
        self.assertIn('{"window_width", w}, {"window_height", h}', store)
        self.assertIn("video_mode_width = 1280", test_cpp)
        self.assertIn("video_mode_height = 720", test_cpp)
        self.assertIn("fit_16_10.height == 1440", test_cpp)
        self.assertIn("fit_ultra.x == 440", test_cpp)
        self.assertNotIn('Change("video_mode_width"', menu)
        self.assertNotIn('Change("video_mode_height"', menu)

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
        self.assertIn("hulk_guest_swap_fps_cap", menu)
        self.assertNotIn('"gpu_guest_fps_limit"', menu)
        self.assertIn("HULK/PC-GRAPHICS", menu)
        self.assertIn('Change("hulk_eor_vulkan_replay", direct_eor ? "true" : "false")', menu)
        self.assertIn('PendingBool("hulk_eor_vulkan_replay")', menu)
        self.assertIn("GetEorWorkerStatus()", menu)
        self.assertIn('Change("native_x_renderer_resolution_scale", "2.0")', menu)
        self.assertIn('Change("native_x_renderer_scaling", "fsr")', menu)
        self.assertIn('Change("native_x_renderer_scaling", "nis")', menu)
        self.assertIn('Change("native_x_renderer_antialiasing", "off")', menu)
        self.assertIn('Change("native_x_renderer_antialiasing", "fxaa")', menu)
        self.assertIn('Change("native_x_renderer_antialiasing", "smaa")', menu)
        self.assertIn('key == "native_x_renderer_antialiasing"', (ROOT/"native_port/src/native_pc_options_store.cpp").read_text())
        self.assertIn('Change("native_x_renderer_sharpness"', menu)
        self.assertIn("DLSS/DLAA requires verified scene", menu)
        config = (ROOT/"native_port/incredible_hulk.toml").read_text()
        self.assertIn('native_x_renderer_scaling = "bilinear"', config)
        self.assertIn("native_x_renderer_sharpness = 0.0", config)

        self.assertIn('Change("hulk_guest_swap_fps_cap", "60")', menu)
        # The actual Xbox guest FPS limit, not a fake on-screen selection.
        for value in ("90", "120", "144", "0"):
            self.assertIn('Change("hulk_guest_swap_fps_cap", "'+value+'")', menu)
        self.assertIn('Change("vsync", vsync ? "true" : "false")', menu)
        self.assertIn("HULK/FPS-CHOICES", menu)
        self.assertIn("Selecting a higher FPS does not change the physics timestep", menu)
        self.assertIn("hulk_guest_swap_fps_cap = 0", config)
        self.assertIn("src/pc_menu.cpp", cmake)
        self.assertIn("src/native_pc_options_store.cpp", cmake)
        self.assertIn("native_pc_config_ = paths.config_path;", app)
        self.assertIn("native_pc_config_, [this]", app)
        # #211: OnCreateDialogs registered the F10 menu before path finalization.
        # It must keep a live reference, never a copy of an empty config path.
        self.assertIn("HULK/PCMENU-CONFIG-LATE", menu)
        self.assertIn("const std::filesystem::path& config_;", menu)
        self.assertNotIn("std::filesystem::path config_;", menu)
        self.assertIn("native_pc_config_ = paths.config_path;", app)
        self.assertIn("hulk::options::Save(config_, key, value)", menu)
        self.assertIn("rex::cvar::SetFlagByName(key,value)", menu)
        self.assertIn("HULK/PCMENU-PERSISTED-PREVIEW", menu)
        self.assertIn("saved_preview_[key]=value", menu)
        self.assertIn('PendingInt("hulk_guest_swap_fps_cap")', menu)
        self.assertIn('PendingBool("vsync")', menu)
        self.assertIn('PendingDouble("native_x_renderer_resolution_scale")', menu)
        self.assertIn("[HULK/PCMENU-SAVE]", menu)
        self.assertIn('REXCVAR_DEFINE_STRING(hulk_bind_back, "F"', inputcode)
        self.assertIn('REXCVAR_DEFINE_STRING(hulk_bind_secondary, "MouseRight"', inputcode)
        self.assertIn("Settings saved successfully.", menu)
        self.assertNotIn("F4 Settings", menu)
        self.assertIn('ImGui::Button("Exit to Windows"', menu)
        self.assertIn('ImGui::Button("Resume game"', menu)
        self.assertIn("PcMenuSetOpen(false)", menu)
        for name in ("hulk_bind_forward","hulk_bind_confirm",
                     "hulk_bind_primary","hulk_bind_back"):
            self.assertIn('Bind("'+name+'")',menu)

if __name__=="__main__":
    unittest.main()
