// Native Windows controls / graceful Exit to Desktop overlay for Hulk.
// The original 360 menu UI is still console art; this PC menu is independent.
// No modifications to gameplay input maps, GPU render hooks or guest assets.
#include "pc_menu.h"
#include "native_pc_options_store.h"
#include "native_pc_bindings.h"
#include "native_resolution.h"
#include "native_gpu_autodiag.h"
#include "eor_ui_original_dds.h"
#include <rex/ui/immediate_drawer.h>
#if defined(HULK_NATIVE_X_LINKED)
#include "eor_xdk_hook_bridge.h"
#endif
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>
#include <rex/logging.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
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
std::atomic<bool> return_to_home{true};
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
    if (return_to_home.exchange(false, std::memory_order_acq_rel))
      active_section_=-1;
    // An optional retail PC 2008 install provides authentic Hulk EoR menu
    // art. The Xbox 360 UIPage remains game-owned; do not patch its guest
    // binary or substitute a guessed LFXT format for the original asset.
    if(!original_menu_art_tried_)LoadOriginalEorMenuArt();
    UploadOriginalEorMenuArt();
    // HULK/PC-MENU-SECTIONS: accessible in-game host overlay, not a
    // counterfeit edit of the Xbox pause-menu or an asset replacement.
    // Do not advertise controls until real CVars or game APIs are wired.
    // HULK/PC-MENU-DPI-RESPONSIVE: fit the options to the client
    // on the owner's 2560x1600 / Windows 175%-DPI screen and on 4:3.
    const float menu_scale = std::clamp(io.FontGlobalScale, 0.75f, 2.5f);
    const float menu_width = std::min(
        1160.0f * menu_scale, std::max(240.0f, io.DisplaySize.x - 32.0f));
    const float menu_height = std::min(
        760.0f * menu_scale, std::max(210.0f, io.DisplaySize.y - 24.0f));
    // A true in-game PC frontend, not a small floating debug/settings box.
    // All size calculations use ImGui logical pixels, not Xbox 720p pixels.
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.065f, 0.082f, 0.061f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.095f, 0.125f, 0.087f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.19f, 0.28f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.46f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.31f, 0.42f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.36f, 0.51f, 0.24f, 1.0f));
    bool open = true;
    const ImGuiWindowFlags native_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##HulkNativeGameMenu", &open, native_flags)) {
      // Fullscreen, DPI-aware Hulk-green frontend with an actual home page,
      // real options, keyboard/mouse navigation and desktop exit confirmation.
      auto* art=ImGui::GetWindowDrawList();
      const auto top=ImGui::GetWindowPos();
      const auto extent=ImGui::GetWindowSize();
      art->AddRectFilledMultiColor(
          top,ImVec2(top.x+extent.x,top.y+extent.y),
          IM_COL32(8,20,13,248),IM_COL32(14,28,16,248),
          IM_COL32(3,8,8,252),IM_COL32(5,14,10,252));
      art->AddRectFilled(
          ImVec2(top.x,top.y),ImVec2(top.x+5.0f*menu_scale,top.y+extent.y),
          IM_COL32(121,203,64,255));
      // Retail Hulk's original PC UI textures are atlases, NOT full
      // screen wallpapers. Use the authored horizontal menu-band sprite
      // and gold selection strip, never scale a complete atlas into a
      // giant illegible square. UVs select only the real source pixels.
      const float art_left=top.x+(extent.x-menu_width)*0.5f;
      const float art_top=top.y+(extent.y-menu_height)*0.5f;
      if(original_bands_gpu_) {
        const ImVec2 p(art_left,art_top);
        art->AddImage(
            reinterpret_cast<ImTextureID>(original_bands_gpu_.get()),
            p,ImVec2(p.x+menu_width,p.y+66.0f*menu_scale),
            ImVec2(0.0f,0.02f),ImVec2(1.0f,0.34f),
            IM_COL32(255,255,255,230));
      }
      if(original_items_gpu_) {
        const ImVec2 p(art_left,art_top+55.0f*menu_scale);
        art->AddImage(
            reinterpret_cast<ImTextureID>(original_items_gpu_.get()),
            p,ImVec2(p.x+menu_width,p.y+5.0f*menu_scale),
            ImVec2(0.0f,0.15f),ImVec2(0.48f,0.22f),
            IM_COL32(255,255,255,235));
      }
      const float panel_left=std::max(16.0f,(extent.x-menu_width)*0.5f);
      const float panel_top=std::max(14.0f,(extent.y-menu_height)*0.5f);
      const float section_width=std::min(220.0f*menu_scale,menu_width*0.32f);
      const float footer_height=std::clamp(
          68.0f*menu_scale,54.0f,menu_height*0.17f);
      const float pane_height=std::max(
          120.0f,menu_height-112.0f*menu_scale-footer_height);
      const float settings_width=std::max(
          110.0f,menu_width-section_width-14.0f*menu_scale);
      ImGui::SetCursorPos(ImVec2(panel_left,panel_top));
      auto* font=ImGui::GetFont();
      const ImVec2 heading=ImGui::GetCursorScreenPos();
      art->AddText(font,37.0f*menu_scale,heading,
                   IM_COL32(187,233,149,255),"THE INCREDIBLE HULK");
      ImGui::Dummy(ImVec2(menu_width,44.0f*menu_scale));
      ImGui::SetCursorPosX(panel_left);
      ImGui::TextDisabled("NATIVE PC EDITION  /  GAME MENU");
      ImGui::SetCursorPosX(panel_left);
      ImGui::Separator();
      ImGui::SetCursorPosX(panel_left);
      ImGui::BeginChild("##hulk_sections",
                        ImVec2(section_width,pane_height),true);
      if(ImGui::Selectable("CONTINUE GAME",false))
        PcMenuSetOpen(false);
      if(ImGui::Selectable("HOME",active_section_==-1))
        active_section_=-1;
      ImGui::Separator();
      ImGui::TextDisabled("SETTINGS");
      // HULK/PC-MENU-SECTIONS: all entries are backed by working UI,
      // never decorative sliders or invented GPU/guest engine controls.
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
                       ImVec2(settings_width,pane_height),true);
      switch (active_section_) {
        case -1: {
          ImGui::Spacing();
          ImGui::TextColored(ImVec4(0.73f,0.91f,0.58f,1.f),
                             "THE CITY IS YOURS");
          ImGui::Separator();
          ImGui::Spacing();
          ImGui::TextWrapped(
              "The Incredible Hulk (2008) - Xbox 360 USA. "
              "A native Windows interface for keyboard, mouse, display, "
              "graphics and gameplay settings.");
          ImGui::Spacing();
          ImGui::Spacing();
          if(ImGui::Button("CONTINUE GAME",ImVec2(240.0f*menu_scale,48.0f*menu_scale)))
            PcMenuSetOpen(false);
          if(ImGui::Button("DISPLAY & GRAPHICS",ImVec2(240.0f*menu_scale,42.0f*menu_scale)))
            active_section_=0;
          if(ImGui::Button("KEYBOARD & MOUSE",ImVec2(240.0f*menu_scale,42.0f*menu_scale)))
            active_section_=4;
          ImGui::Spacing();
          ImGui::Separator();
          const bool native_x=rex::cvar::Query<bool>("native_x_renderer");
          const bool eor_requested=rex::cvar::Query<bool>("hulk_eor_vulkan_replay");
          ImGui::Text("Renderer: %s",
              eor_requested ? "Direct EoR requested (source-gated)" :
              native_x ? "NativeX Vulkan compatibility" : "Xenos Vulkan");
#if defined(HULK_NATIVE_X_LINKED)
          const auto replay=hulk::eor::xdk::GetEorWorkerStatus();
          ImGui::Text("EoR Vulkan backbuffer frames: %llu",
              static_cast<unsigned long long>(replay.ready_backbuffers));
#endif
          ImGui::TextDisabled(
              "Esc / F10: menu  |  Enter: original game Start");
          break;
        }
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
          if (!custom_size_initialized_) {
            custom_size_[0] = std::max(320, current_w);
            custom_size_[1] = std::max(200, current_h);
            custom_size_initialized_ = true;
          }
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
                  custom_size_[0] = static_cast<int>(mode.width);
                  custom_size_[1] = static_cast<int>(mode.height);
                }
              }
              if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }
          // Custom values are edited locally and committed as an atomic W/H
          // pair only when clicked, never on every keystroke or ImGui frame.
          // This supports arbitrary aspect and real pixel sizes rather than
          // artificially restricting output to console/16:9 presets.
          ImGui::InputInt2("Custom width / height", custom_size_);
          const bool custom_valid = custom_size_[0] > 0 &&
              custom_size_[1] > 0 &&
              display::IsSupportedHostSize(
                  static_cast<uint32_t>(custom_size_[0]),
                  static_cast<uint32_t>(custom_size_[1]));
          if (ImGui::Button("Save custom size (restart)")) {
            last_save_ok_ = custom_valid && options::SaveWindowSize(
                config_, static_cast<uint32_t>(custom_size_[0]),
                static_cast<uint32_t>(custom_size_[1]));
            if (*last_save_ok_) {
              pending_window_width_ = static_cast<uint32_t>(custom_size_[0]);
              pending_window_height_ = static_cast<uint32_t>(custom_size_[1]);
            }
          }
          if(!custom_valid)
            ImGui::TextDisabled("Choose 320..16384 pixels, up to 64M total pixels.");
          else
            ImGui::Text("Custom aspect %.3f:1",
                        double(custom_size_[0])/custom_size_[1]);
          ImGui::TextWrapped(
              "Fullscreen uses desktop resolution. Windowed sizes are host-only. "
              "16:10, 21:9, 32:9, 4:3, square and portrait are accepted. "
              "Without a verified USA camera the scene is centered, not stretched.");
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
      bool observe_eor=PendingBool("hulk_eor_xdk_capture");
      if(ImGui::Checkbox("Capture original Xbox Draw/VS/PS (read-only, restart)",
                         &observe_eor))
        Change("hulk_eor_xdk_capture", observe_eor ? "true" : "false");
      ImGui::TextWrapped(
          "Read-only original shader source verification without native "
          "Vulkan takeover. Capture is optional: compatibility Xenos remains "
          "the renderer. Full source shader and pass banks can load separately.");
      bool original_mesh_capture=PendingBool("hulk_eor_xdk_source_mesh_capture");
      if(ImGui::Checkbox("Capture original GPU vertex/index bytes (read-only, restart)",
                         &original_mesh_capture)) {
        Change("hulk_eor_xdk_source_mesh_capture",
               original_mesh_capture ? "true" : "false");
        if(original_mesh_capture)
          Change("hulk_eor_xdk_capture","true");
      }
      ImGui::TextWrapped(
          "Exact original XDK bound VB/IB allocations: optional 64 MiB "
          "per-Present cap, no gameplay/GPU writes, no native renderer "
          "takeover and no guessed model or texture ownership.");
      bool direct_eor=PendingBool("hulk_eor_vulkan_replay");
      if(ImGui::Checkbox("Direct EoR Vulkan replay (experimental, restart)",
                         &direct_eor))
        Change("hulk_eor_vulkan_replay", direct_eor ? "true" : "false");
      ImGui::TextWrapped(
          "Native EoR uses the same Vulkan worker and one backbuffer Present "
          "only when exact original Draw, mesh/material, shader and camera "
          "owners have been verified. Otherwise compatibility rendering stays active.");
#if defined(HULK_NATIVE_X_LINKED)
      const auto coverage=hulk::eor::xdk::GetOriginalEorSourceCoverage();
      ImGui::Text("Source original PPC frame: %llu; XDK Draws: %u",
          static_cast<unsigned long long>(coverage.original_frame_sequence),
          coverage.original_xdk_draws);
      ImGui::Text("Original VS/PS header SHA: %u / %u",
          coverage.original_vs_headers,coverage.original_ps_headers);
      ImGui::Text("Verified original shader pairs: %u; effect passes: %u",
          coverage.original_vs_ps_pairs,coverage.original_effect_passes);
      ImGui::Text("Actual bound VB0: %u; IB: %u; indexed pairs: %u",
          coverage.original_live_vb0,coverage.original_live_ib,
          coverage.original_indexed_with_exact_buffer_bytes);
      ImGui::Text("Original source timeline: %s",
          coverage.complete_xdk_timeline ? "complete" : "not verified");
      ImGui::TextWrapped(
          "These are actual live guest evidence counts, not native Vulkan "
          "readiness. Missing original game mesh/material ownership still "
          "prevents takeover of the host frame.");
      const auto status=hulk::eor::xdk::GetEorWorkerStatus();
      ImGui::Text("Native EoR verified backbuffers: %llu",
          static_cast<unsigned long long>(status.ready_backbuffers));
      ImGui::Text("Native GPU submission failures: %llu",
          static_cast<unsigned long long>(status.failed_after_gpu));
#endif
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
      ImGui::SetCursorPosX(panel_left);
      ImGui::Separator();
      ImGui::SetCursorPosX(panel_left);
      ImGui::TextDisabled("Changes saved to incredible_hulk.toml"
                          "  |  F8: recapture mouse  |  F10 / Esc: game menu");
      if (last_save_ok_.has_value()) {
        ImGui::SetCursorPosX(panel_left);
        if (*last_save_ok_) {
          ImGui::TextUnformatted("Settings saved successfully.");
        } else {
          ImGui::TextColored(ImVec4(1.0f, 0.52f, 0.30f, 1.0f),
                             "Could not save TOML; current value is temporary.");
        }
      }
      ImGui::SetCursorPosX(panel_left);
      if (ImGui::Button("Resume game", ImVec2(210, 36))) {
        PcMenuSetOpen(false);
      }
      ImGui::SameLine();
      if (ImGui::Button("Exit to Windows", ImVec2(210, 36))) {
        ImGui::OpenPopup("Exit to desktop?");
      }
      // Explicit exit confirmation: never discard a session because a
      // controller/keyboard selection moved onto the last menu item.
      if(ImGui::BeginPopupModal("Exit to desktop?",nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Close The Incredible Hulk and return to Windows?");
        ImGui::Spacing();
        if(ImGui::Button("Return to game",ImVec2(150,36))) {
          ImGui::CloseCurrentPopup();
          PcMenuSetOpen(false);
        }
        ImGui::SameLine();
        if(ImGui::Button("Confirm exit",ImVec2(150,36))) {
          ReportAutomaticGpuDiagnostics();
          ImGui::CloseCurrentPopup();
          PcMenuSetOpen(false);
          if(on_exit_)on_exit_();
        }
        ImGui::EndPopup();
      }
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    if (!open) PcMenuSetOpen(false);
  }
 private:
  void LoadOriginalEorMenuArt() {
    original_menu_art_tried_=true;
    std::vector<std::filesystem::path> folders;
    if(const char* env=std::getenv("HULK_PC2008_UI_DIR");env && *env)
      folders.emplace_back(env);
    if(!config_.empty())
      folders.push_back(config_.parent_path());
    std::error_code ec;
    const auto cwd=std::filesystem::current_path(ec);
    if(!ec)folders.push_back(cwd);
    for(const auto& root:folders) {
      try {
        const auto index=root/"indexPC.ind";
        auto textures=root/"textures(1).arc";
        if(!std::filesystem::is_regular_file(textures))
          textures=root/"TEXTURES.ARC";
        if(!std::filesystem::is_regular_file(index) ||
           !std::filesystem::is_regular_file(textures))
          continue;
        auto bands=eor::ui::LoadPcMenuArt(
            index,textures,"ui_menubands_da");
        auto items=eor::ui::LoadPcMenuArt(
            index,textures,"ui_menuitems_da");
        original_bands_=std::move(bands);
        original_items_=std::move(items);
        REXLOG_INFO("[HULK/EOR-UI] authentic original PC 2008 "
                    "menu art decoded; host Vulkan texture pending");
        return;
      } catch(const std::exception& error) {
        REXLOG_WARN("[HULK/EOR-UI] original PC menu source not usable: {}",
                    error.what());
      }
    }
  }
  void UploadOriginalEorMenuArt() {
    if(original_bands_.rgba.empty() || original_items_.rgba.empty())
      return;
    auto* current=imgui_drawer()->immediate_drawer();
    if(!current)return;
    if(original_menu_art_drawer_!=current) {
      // A texture from a different immediate renderer must never be used
      // on the current Vulkan UI device after a renderer reattachment.
      original_items_gpu_.reset();
      original_bands_gpu_.reset();
      original_menu_art_drawer_=current;
    }
    if(original_bands_gpu_ && original_items_gpu_)return;
    try {
      if(!original_bands_gpu_)
        original_bands_gpu_=current->CreateTexture(
            original_bands_.width,original_bands_.height,
            rex::ui::ImmediateTextureFilter::kLinear,false,
            original_bands_.rgba.data());
      if(!original_items_gpu_)
        original_items_gpu_=current->CreateTexture(
            original_items_.width,original_items_.height,
            rex::ui::ImmediateTextureFilter::kLinear,false,
            original_items_.rgba.data());
    } catch(const std::exception& error) {
      original_bands_gpu_.reset();
      original_items_gpu_.reset();
      original_bands_={};original_items_={};
      REXLOG_WARN("[HULK/EOR-UI] host original PC texture upload rejected: {}",
                  error.what());
    }
  }
  bool original_menu_art_tried_=false;
  rex::ui::ImmediateDrawer* original_menu_art_drawer_=nullptr;
  eor::ui::OriginalPcUiImage original_bands_,original_items_;
  std::unique_ptr<rex::ui::ImmediateTexture> original_bands_gpu_,original_items_gpu_;
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
  int custom_size_[2] = {1280, 720};
  bool custom_size_initialized_ = false;
  float mouse_x_edit_value_ = 1.0f;
  float mouse_y_edit_value_ = 1.0f;
  bool mouse_x_edit_active_ = false;
  bool mouse_y_edit_active_ = false;
  float sharpness_edit_value_ = 0.0f;
  bool sharpness_edit_active_ = false;
  std::optional<bool> last_save_ok_;
  int active_section_ = -1;
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
  if(open && !old) return_to_home.store(true, std::memory_order_release);
  if (old != open) REXLOG_INFO("[HULK/PCMENU] visible={}", open);
  if (old && !open) GameWindowMenuClosed();
  if (game_window) game_window->RequestPaint();
}
void PcMenuToggle() { PcMenuSetOpen(!PcMenuIsOpen()); }
} // namespace hulk
