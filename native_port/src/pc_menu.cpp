// Native Windows controls / graceful Exit to Desktop overlay for Hulk.
// The original 360 menu UI is still console art; this PC menu is independent.
// No modifications to gameplay input maps, GPU render hooks or guest assets.
#include "pc_menu.h"
#include "native_pc_options_store.h"
#include "native_pc_bindings.h"
#include "native_resolution.h"
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>
#include <rex/logging.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <string_view>
#include <optional>
#include <string>
#include <utility>

namespace hulk {
// Window focus/capture is owned by game_window.cpp; do not invent a second
// mouse-capture state in this diagnostic overlay.
void GameWindowMenuClosed();
namespace {
std::atomic<bool> visible{false};
rex::ui::Window* game_window = nullptr; // accessed only on host UI thread

class PcMenu final : public rex::ui::ImGuiDialog {
 public:
  PcMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
         const std::filesystem::path& existing_config,
         std::function<void()> exit)
      : ImGuiDialog(drawer), config_(existing_config),
        on_exit_(std::move(exit)) {
    game_window = window;
  }
  ~PcMenu() override {
    game_window = nullptr;
    visible.store(false, std::memory_order_release);
  }
  bool WantsContinuousPaint() override {
    return visible.load(std::memory_order_acquire);
  }
 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!visible.load(std::memory_order_acquire)) return;
    // HULK/PC-MENU-SECTIONS: accessible in-game host overlay, not a
    // counterfeit edit of the Xbox pause-menu or an asset replacement.
    // Do not advertise controls until real CVars or game APIs are wired.
    // HULK/PC-MENU-DPI-RESPONSIVE: fit the options to the client
    // on the owner's 2560x1600 / Windows 175%-DPI screen and on 4:3.
    const float menu_scale = std::clamp(io.FontGlobalScale, 0.75f, 2.5f);
    const float menu_width = std::min(
        850.0f * menu_scale, std::max(240.0f, io.DisplaySize.x - 24.0f));
    const float menu_height = std::min(
        610.0f * menu_scale, std::max(200.0f, io.DisplaySize.y - 24.0f));
    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f,
                                   io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.065f, 0.082f, 0.061f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.095f, 0.125f, 0.087f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.19f, 0.28f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.46f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.31f, 0.42f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.36f, 0.51f, 0.24f, 1.0f));
    bool open = true;
    if (ImGui::Begin("The Incredible Hulk | PC Settings (F10)", &open,
                     ImGuiWindowFlags_NoCollapse)) {
      ImGui::TextUnformatted("THE INCREDIBLE HULK");
      ImGui::SameLine();
      ImGui::TextDisabled("  /  PC SETTINGS");
      ImGui::Separator();
      const float section_width =
           std::min(176.0f * menu_scale, menu_width * 0.32f);
       const float footer_height = std::clamp(
           66.0f * menu_scale, 54.0f, menu_height * 0.18f);
       ImGui::BeginChild("##hulk_sections",
                         ImVec2(section_width, -footer_height), true);
      static constexpr const char* kSections[] = {
          "Display", "Graphics", "Post-processing", "Audio",
          "Controls", "Accessibility", "Enhancements"};
      for (int i = 0; i < 7; ++i) {
        if (ImGui::Selectable(kSections[i], active_section_ == i))
          active_section_ = i;
      }
      ImGui::EndChild();
      ImGui::SameLine();
      ImGui::BeginChild("##hulk_settings",
                         ImVec2(0.0f, -footer_height), true);
      switch (active_section_) {
        case 0: {
          ImGui::TextUnformatted("Display");
          ImGui::Separator();
          // HULK/HOST-PIXELS: ImGui logical size times framebuffer DPI,
          // not the Xbox guest's fixed 1280x720 render-target dimensions.
          const float pixel_w = io.DisplaySize.x * io.DisplayFramebufferScale.x;
          const float pixel_h = io.DisplaySize.y * io.DisplayFramebufferScale.y;
          if (pixel_w >= 1.0f && pixel_h >= 1.0f &&
              pixel_w <= 16384.0f && pixel_h <= 16384.0f) {
            const auto fit = display::FitOriginalPicture(
                static_cast<uint32_t>(pixel_w),
                static_cast<uint32_t>(pixel_h));
            if (fit.valid) {
              ImGui::Text("Host framebuffer: %.0f x %.0f", pixel_w, pixel_h);
              ImGui::Text("Host 16:9 fit: %u x %u; guest-to-host pixel ratio %.3fx",
                          fit.width, fit.height, fit.scale);
              if (fit.has_bars)
                ImGui::TextDisabled("Bars: x=%u y=%u (camera hook pending)",
                                    fit.x, fit.y);
            }
          }
          bool fullscreen = pending_fullscreen_.value_or(
              PendingBool("fullscreen"));
          if (ImGui::Checkbox("Fullscreen (restart to apply)", &fullscreen)) {
            const bool saved = options::Save(
                config_, "fullscreen", fullscreen ? "true" : "false");
            last_save_ok_ = saved;
            if (saved) pending_fullscreen_ = fullscreen;
          }
          int current_w = rex::cvar::Query<int32_t>("window_width");
          int current_h = rex::cvar::Query<int32_t>("window_height");
          if (pending_window_width_ && pending_window_height_) {
            current_w = static_cast<int>(*pending_window_width_);
            current_h = static_cast<int>(*pending_window_height_);
          }
          const std::string current_size =
              current_w > 0 && current_h > 0
                  ? std::to_string(current_w) + "x" +
                    std::to_string(current_h) : "App default";
          if (ImGui::BeginCombo("Window size (restart)",
                                current_size.c_str())) {
            for (const auto& mode : display::kWindowPresets) {
              const bool selected =
                  current_w == static_cast<int>(mode.width) &&
                  current_h == static_cast<int>(mode.height);
              const std::string label(mode.label);
              if (ImGui::Selectable(label.c_str(), selected)) {
                const bool saved = options::SaveWindowSize(
                    config_, mode.width, mode.height);
                last_save_ok_ = saved;
                if (saved) {
                  pending_window_width_ = mode.width;
                  pending_window_height_ = mode.height;
                }
              }
              if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }
          ImGui::TextWrapped(
              "Fullscreen uses desktop pixels. Windowed sizes are host-only. "
              "True 16:10/21:9 camera widening needs a verified USA Hulk "
              "projection hook; the original HUD is not stretched.");
          ImGui::Separator();
      const int32_t guest_fps_limit = PendingInt("hulk_guest_swap_fps_cap");
      if (guest_fps_limit == 0) {
        ImGui::TextUnformatted("Guest FPS limit: Unlimited (game may still run 30)");
      } else {
        ImGui::Text("Guest FPS limit: %d", guest_fps_limit);
      }
      if (ImGui::Button("30 FPS (console physics)")) {
        Change("hulk_guest_swap_fps_cap", "30");
      }
      ImGui::SameLine();
      if (ImGui::Button("60 FPS (experimental)")) {
        Change("hulk_guest_swap_fps_cap", "60");
      }
      // HULK/FPS-CHOICES: the guest frame limiter is separate from Vulkan
      // presentation. Do NOT alter guest clock / physics timestep here:
      // the game's PPC simulation still needs a verified 30Hz decoupling fix.
      ImGui::TextUnformatted("Higher frame rates (experimental physics)");
      if (ImGui::Button("90 FPS")) {
        Change("hulk_guest_swap_fps_cap", "90");
      }
      ImGui::SameLine();
      if (ImGui::Button("120 FPS")) {
        Change("hulk_guest_swap_fps_cap", "120");
      }
      ImGui::SameLine();
      if (ImGui::Button("144 FPS")) {
        Change("hulk_guest_swap_fps_cap", "144");
      }
      ImGui::SameLine();
      if (ImGui::Button("Unlimited FPS")) {
        Change("hulk_guest_swap_fps_cap", "0");
      }
      bool vsync = PendingBool("vsync");
      if (ImGui::Checkbox("Vertical synchronization", &vsync)) {
        Change("vsync", vsync ? "true" : "false");
      }
      ImGui::TextWrapped(
          "30 FPS matches the console update cadence. At 60+ FPS some "
          "animations and rigid-body interactions can still run incorrectly. "
          "Selecting a higher FPS does not change the physics timestep.");
      ImGui::TextUnformatted("Changes are saved to the existing TOML automatically.");
          break;
        }
        case 1: {
          ImGui::TextUnformatted("Graphics");
          ImGui::Separator();
      // HULK/PC-GRAPHICS: these NativeX settings are read at startup.
      // Expose only renderer-supported paths; do not claim DLSS works
      // before a verified main-scene view and motion vectors are available.
      ImGui::Separator();
      ImGui::TextUnformatted("PC graphics (restart game to apply)");
      ImGui::TextWrapped("Current NativeX Vulkan: host-scaled XDK/PM4 compatibility rendering. Full Edge of Reality native passes and 16:10 camera are not active yet.");
      const double native_scale =
          PendingDouble("native_x_renderer_resolution_scale");
      ImGui::TextUnformatted(native_scale <= 0.0
          ? "NativeX host raster scale: Auto (game targets remain Xbox-sized)"
          : "NativeX host raster scale: fixed multiplier");
      if (native_scale > 0.0)
        ImGui::Text("NativeX physical target multiplier: %.2fx", native_scale);
      // HULK/NATIVE-AUTO-PIXELS: this controls physical NativeX targets,
      // not independent EoR render passes, all shader mips or the USA camera.
      // Do not label it as a complete native 16:10 renderer. Guest 1280x720
      // descriptors remain in this compatibility backend.
      if (ImGui::Button("Host raster / Auto")) {
        Change("native_x_renderer_resolution_scale", "0.0");
      }
      ImGui::SameLine();
      if (ImGui::Button("Rendering x1")) {
        Change("native_x_renderer_resolution_scale", "1.0");
      }
      ImGui::SameLine();
      if (ImGui::Button("Rendering x2")) {
        Change("native_x_renderer_resolution_scale", "2.0");
      }
          break;
        }
        case 2: {
          ImGui::TextUnformatted("Post-processing");
          ImGui::Separator();
      // HULK/GPU-SAMPLE-BUDGET: original guest MSAA remains 4x.
      // Only the additional full-scene NativeX post-AA pass changes.
      const std::string post_aa = Bind("native_x_renderer_antialiasing");
      ImGui::Text("Extra post-AA: %s (restart)", post_aa.c_str());
      if (ImGui::Button("Original 4x MSAA only (lower GPU work)"))
        Change("native_x_renderer_antialiasing", "off");
      if (ImGui::Button("Post-FXAA"))
        Change("native_x_renderer_antialiasing", "fxaa");
      ImGui::SameLine();
      if (ImGui::Button("Post-SMAA"))
        Change("native_x_renderer_antialiasing", "smaa");
      ImGui::TextWrapped(
          "Keeps native host pixels and the original 4x MSAA scene. "
          "Disabling extra AA does not fix PM4 geometry or missing textures.");
      const std::string scaling = Bind("native_x_renderer_scaling");
      ImGui::Text("Presentation filter: %s", scaling.c_str());
      if (ImGui::Button("Bilinear + CAS")) {
        Change("native_x_renderer_scaling", "bilinear");
      }
      ImGui::SameLine();
      if (ImGui::Button("FSR 1")) {
        Change("native_x_renderer_scaling", "fsr");
      }
      ImGui::SameLine();
      if (ImGui::Button("NIS")) {
        Change("native_x_renderer_scaling", "nis");
      }
      // HULK/PC-MENU-NO-DISK-DRAG: native UI slider state is local
      // while dragging. Flush the shared TOML only on commit/release;
      // rewriting it every mouse movement would itself cause stutter.
      float sharpness = sharpness_edit_active_
          ? sharpness_edit_value_
          : static_cast<float>(
                PendingDouble("native_x_renderer_sharpness"));
      if (sharpness < 0.0f) sharpness = 0.0f;
      if (sharpness > 0.5f) sharpness = 0.5f;
      const bool sharpness_changed =
          ImGui::SliderFloat("Sharpening", &sharpness, 0.0f, 0.5f, "%.2f");
      if (sharpness_changed) sharpness_edit_value_ = sharpness;
      if (ImGui::IsItemActive()) sharpness_edit_active_ = true;
      if (ImGui::IsItemDeactivatedAfterEdit() ||
          (sharpness_changed && !ImGui::IsItemActive())) {
        Change("native_x_renderer_sharpness",
               std::to_string(sharpness_edit_value_));
        sharpness_edit_active_ = false;
      }
      ImGui::TextUnformatted("Note: sharpen cannot repair missing shader samples.");
      ImGui::TextUnformatted("DLSS/DLAA requires verified scene and motion vectors.");
      ImGui::TextUnformatted("Graphics changes are saved; restart game to apply.");
          break;
        }
        case 3: {
          ImGui::TextUnformatted("Audio");
          ImGui::Separator();
          ImGui::TextWrapped(
              "Audio still uses the game's current mixer. Native volume "
              "controls are not wired to a verified engine API yet.");
          ImGui::TextDisabled("No inactive sliders or fake apply buttons.");
          break;
        }
        case 4: {
          ImGui::TextUnformatted("Controls");
          ImGui::Separator();
      const std::string w = Bind("hulk_bind_forward");
      const std::string a = Bind("hulk_bind_left");
      const std::string s = Bind("hulk_bind_backward");
      const std::string d = Bind("hulk_bind_right");
      ImGui::Text("Move: %s / %s / %s / %s", w.c_str(), a.c_str(), s.c_str(), d.c_str());
      ImGui::TextUnformatted("Look: mouse");
      // Black V3-style separate axis controls; both routes use live CVars.
      bool invert_x = PendingBool("hulk_mouse_invert_x");
      if (ImGui::Checkbox("Invert mouse X", &invert_x))
        Change("hulk_mouse_invert_x", invert_x ? "true" : "false");
      bool invert_y = PendingBool("hulk_mouse_invert_y");
      if (ImGui::Checkbox("Invert mouse Y", &invert_y))
        Change("hulk_mouse_invert_y", invert_y ? "true" : "false");
      DrawMouseMultiplier("Mouse horizontal speed",
                          "hulk_mouse_horizontal_multiplier",
                          mouse_x_edit_value_, mouse_x_edit_active_);
      DrawMouseMultiplier("Mouse vertical speed",
                          "hulk_mouse_vertical_multiplier",
                          mouse_y_edit_value_, mouse_y_edit_active_);
      const std::string jump = Bind("hulk_bind_confirm");
      const std::string action = Bind("hulk_bind_primary");
      const std::string secondary = Bind("hulk_bind_secondary");
      const std::string back = Bind("hulk_bind_back");
      ImGui::Text("Jump / Confirm: %s", jump.c_str());
      ImGui::Text("Primary attack: %s", action.c_str());
      ImGui::Text("Heavy attack (right mouse): %s", secondary.c_str());
      ImGui::Text("Grab / Interact (F): %s", back.c_str());
      const std::string lb = Bind("hulk_bind_lb");
      const std::string rb = Bind("hulk_bind_rb");
      const std::string lt = Bind("hulk_bind_lt");
      const std::string rt = Bind("hulk_bind_rt");
      ImGui::Text("Abilities: %s / %s", lb.c_str(), rb.c_str());
      ImGui::Text("Triggers: %s / %s", lt.c_str(), rt.c_str());
      ImGui::Text("Pause / Start: %s", Bind("hulk_bind_start").c_str());
      ImGui::Text("Walk: %s", Bind("hulk_bind_walk").c_str());
          ImGui::Separator();
          ImGui::TextUnformatted("Keyboard / Mouse bindings");
          ImGui::TextWrapped("Choose a supported key for each action."
                             " Changes apply immediately and are saved.");
          // HULK/NATIVE-REMAP: source-supported actions only. The token
          // list is shared with the independent native TOML writer,
          // so the UI cannot claim to bind unsupported virtual keys.
          static constexpr std::pair<const char*, const char*>
              kRebindActions[] = {
              {"Move forward", "hulk_bind_forward"},
              {"Move backward", "hulk_bind_backward"},
              {"Move left", "hulk_bind_left"},
              {"Move right", "hulk_bind_right"},
              {"Jump / Confirm", "hulk_bind_confirm"},
              {"Attack", "hulk_bind_primary"},
              {"Secondary attack", "hulk_bind_secondary"},
              {"Back / Cancel", "hulk_bind_back"},
              {"Alternate back", "hulk_bind_alt_back"},
              {"Left ability", "hulk_bind_lb"},
              {"Right ability", "hulk_bind_rb"},
              {"Left trigger", "hulk_bind_lt"},
              {"Right trigger", "hulk_bind_rt"},
              {"Pause", "hulk_bind_start"},
              {"Walk", "hulk_bind_walk"},
              {"Menu up", "hulk_bind_menu_up"},
              {"Menu down", "hulk_bind_menu_down"},
              {"Menu left", "hulk_bind_menu_left"},
              {"Menu right", "hulk_bind_menu_right"},
              {"Select / Back button", "hulk_bind_select"},
              {"Right stick click", "hulk_bind_rstick_click"},
          };
          for (const auto& [label, key] : kRebindActions) {
            ImGui::PushID(key);
            const std::string current = Bind(key);
            if (ImGui::BeginCombo(label, current.c_str())) {
              for (auto token : options::kBindingTokens) {
                const bool chosen = token == current;
                const std::string supported(token);
                if (ImGui::Selectable(supported.c_str(), chosen)) {
                  Change(key, supported);
                }
                if (chosen) ImGui::SetItemDefaultFocus();
              }
              ImGui::EndCombo();
            }
            ImGui::PopID();
          }
          ImGui::TextDisabled("Mouse camera and movement simulation are"
                              " still being verified separately.");
          break;
        }
        case 5: {
          ImGui::TextUnformatted("Accessibility");
          ImGui::Separator();
          ImGui::TextWrapped(
              "Mouse and keyboard controls are available in Controls. "
              "Subtitles and interface scale need verified Hulk UI hooks "
              "before they can be offered as working options.");
          ImGui::TextDisabled("No unimplemented accessibility toggles.");
          break;
        }
        case 6: {
          ImGui::TextUnformatted("Enhancements");
          ImGui::Separator();
          ImGui::Text("Vulkan NativeXRenderer: %s",
              rex::cvar::Query<bool>("native_x_renderer") ? "Enabled" : "Disabled");
          ImGui::Text("Anisotropic override: %d",
              rex::cvar::Query<int32_t>("anisotropic_override"));
          ImGui::Text("Persistent pipeline cache: %s",
              rex::cvar::Query<bool>("native_x_renderer_pipeline_storage")
                  ? "Enabled" : "Disabled");
          ImGui::TextWrapped(
              "Verified renderer options are in Graphics and "
              "Post-processing. DLSS/DLAA and uncapped physics are "
              "not presented as working enhancements.");
          break;
        }
      }
      ImGui::EndChild();
      ImGui::Separator();
      ImGui::TextDisabled("Changes saved to incredible_hulk.toml"
                          "  |  F8: recapture mouse  |  F10: close settings");
      if (last_save_ok_.has_value()) {
        if (*last_save_ok_) {
          ImGui::TextUnformatted("Settings saved successfully.");
        } else {
          ImGui::TextColored(ImVec4(1.0f, 0.52f, 0.30f, 1.0f),
                             "Could not save TOML; current value is temporary.");
        }
      }
      if (ImGui::Button("Resume game", ImVec2(210, 36))) {
        PcMenuSetOpen(false);
      }
      ImGui::SameLine();
      if (ImGui::Button("Exit to Windows", ImVec2(210, 36))) {
        // Window closes by ReXApp::OnClosing: TerminateTitle + flush.
        PcMenuSetOpen(false);
        if (on_exit_) on_exit_();
      }
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    if (!open) PcMenuSetOpen(false);
  }
 private:
  // No F4 detour: save the same resolved game TOML that ReXGlue loaded.
   // HULK/PCMENU-CONFIG-LATE: OnCreateDialogs can precede OnFinalizePaths.
   // Hold a REFERENCE to IncredibleHulkApp::native_pc_config_ instead of
   // freezing the initially empty path when the ImGui window is created.
   // The owning app outlives this dialog and destroys it in OnShutdown.
  // Preserve all unrelated renderer/input options and the user's data root.
  // HULK/PCMENU-PERSISTED-PREVIEW: the on-disk user choice is authoritative.
  // Startup-only CVars MUST NOT snap the visible setting back next frame.
  std::string Bind(const char* key) const {
    const auto found=saved_preview_.find(key);
    return found!=saved_preview_.end() ? found->second
        : rex::cvar::Query<std::string>(key);
  }
  bool PendingBool(const char* key) const {
    const auto found=saved_preview_.find(key);
    return found!=saved_preview_.end() ? found->second=="true"
        : rex::cvar::Query<bool>(key);
  }
  int32_t PendingInt(const char* key) const {
    const auto found=saved_preview_.find(key);
    return found!=saved_preview_.end() ? std::stoi(found->second)
        : rex::cvar::Query<int32_t>(key);
  }
  double PendingDouble(const char* key) const {
    const auto found=saved_preview_.find(key);
    return found!=saved_preview_.end() ? std::stod(found->second)
        : rex::cvar::Query<double>(key);
  }
  void Change(const char* key, const std::string& value) {
    const bool saved=hulk::options::Save(config_, key, value);
    last_save_ok_=saved;
    if (!saved) {
      std::error_code ec;
      REXLOG_ERROR("[HULK/PCMENU-SAVE] REJECTED key={} exists={} "
                   "regular={} (unwritable, duplicate or invalid value)",
                   key,std::filesystem::exists(config_,ec),
                   std::filesystem::is_regular_file(config_,ec));
      return;
    }
    saved_preview_[key]=value;
    const std::string_view name(key);
    const bool hot=name.starts_with("hulk_bind_") ||
                   name.starts_with("hulk_mouse_") ||
                   name=="vsync" || name=="hulk_guest_swap_fps_cap";
    const bool applied=hot && rex::cvar::SetFlagByName(key,value);
    REXLOG_INFO("[HULK/PCMENU-SAVE] key={} persisted=true "
                "live_applied={} restart_required={} value={}",
                key,applied,!applied,value);
  }
  void DrawMouseMultiplier(const char* label, const char* key,
                           float& pending, bool& editing) {
    // Commit once when releasing the slider; no repeated disk writes.
    float value = editing ? pending : static_cast<float>(PendingDouble(key));
    value = std::clamp(value, 0.25f, 4.0f);
    const bool changed = ImGui::SliderFloat(label, &value, 0.25f, 4.0f, "%.2fx");
    if (changed) pending = value;
    if (ImGui::IsItemActive()) editing = true;
    if (ImGui::IsItemDeactivatedAfterEdit() ||
        (changed && !ImGui::IsItemActive())) {
      Change(key, std::to_string(pending));
      editing = false;
    }
  }
  const std::filesystem::path& config_;
  std::unordered_map<std::string,std::string> saved_preview_;
  std::optional<bool> pending_fullscreen_;
  std::optional<uint32_t> pending_window_width_;
  std::optional<uint32_t> pending_window_height_;
  float mouse_x_edit_value_ = 1.0f;
  float mouse_y_edit_value_ = 1.0f;
  bool mouse_x_edit_active_ = false;
  bool mouse_y_edit_active_ = false;
  float sharpness_edit_value_ = 0.0f;
  bool sharpness_edit_active_ = false;
  std::optional<bool> last_save_ok_;
  int active_section_ = 0;
  std::function<void()> on_exit_;
};
} // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreatePcMenu(
    rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
    const std::filesystem::path& existing_config,
    std::function<void()> on_exit) {
  return std::make_unique<PcMenu>(drawer, window, existing_config,
                                  std::move(on_exit));
}
bool PcMenuIsOpen() {
  return visible.load(std::memory_order_acquire);
}
void PcMenuSetOpen(bool open) {
  const bool old = visible.exchange(open, std::memory_order_acq_rel);
  if (old != open) REXLOG_INFO("[HULK/PCMENU] visible={}", open);
  if (old && !open) GameWindowMenuClosed();
  if (game_window) game_window->RequestPaint();
}
void PcMenuToggle() { PcMenuSetOpen(!PcMenuIsOpen()); }
} // namespace hulk
