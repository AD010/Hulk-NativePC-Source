// Native Windows controls / graceful Exit to Desktop overlay for Hulk.
// The original 360 menu UI is still console art; this PC menu is independent.
// No modifications to gameplay input maps, GPU render hooks or guest assets.
#include "pc_menu.h"
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>
#include <rex/logging.h>
#include <imgui.h>
#include <atomic>
#include <functional>
#include <string>
#include <utility>

namespace hulk {
// Window focus/capture is owned by game_window.cpp; do not invent a second
// mouse-capture state in this diagnostic overlay.
void GameWindowMenuClosed();
namespace {
std::atomic<bool> visible{false};
rex::ui::Window* game_window = nullptr; // accessed only on host UI thread

std::string Bind(const char* name) {
  return rex::cvar::Query<std::string>(name);
}

class PcMenu final : public rex::ui::ImGuiDialog {
 public:
  PcMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
         std::function<void()> exit)
      : ImGuiDialog(drawer), on_exit_(std::move(exit)) {
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
  void OnDraw(ImGuiIO&) override {
    if (!visible.load(std::memory_order_acquire)) return;
    ImGui::SetNextWindowSize(ImVec2(610.0f, 590.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("The Incredible Hulk - NativePC (F10)", &open,
                     ImGuiWindowFlags_NoCollapse)) {
      ImGui::TextUnformatted("PC keyboard and mouse");
      ImGui::Separator();
      const std::string w = Bind("hulk_bind_forward");
      const std::string a = Bind("hulk_bind_left");
      const std::string s = Bind("hulk_bind_backward");
      const std::string d = Bind("hulk_bind_right");
      ImGui::Text("Move: %s / %s / %s / %s", w.c_str(), a.c_str(), s.c_str(), d.c_str());
      ImGui::TextUnformatted("Look: mouse");
      const std::string jump = Bind("hulk_bind_confirm");
      const std::string action = Bind("hulk_bind_primary");
      const std::string secondary = Bind("hulk_bind_secondary");
      const std::string back = Bind("hulk_bind_back");
      ImGui::Text("Jump / Confirm: %s", jump.c_str());
      ImGui::Text("Primary attack: %s", action.c_str());
      ImGui::Text("Secondary attack: %s", secondary.c_str());
      ImGui::Text("Back / Cancel: %s", back.c_str());
      const std::string lb = Bind("hulk_bind_lb");
      const std::string rb = Bind("hulk_bind_rb");
      const std::string lt = Bind("hulk_bind_lt");
      const std::string rt = Bind("hulk_bind_rt");
      ImGui::Text("Abilities: %s / %s", lb.c_str(), rb.c_str());
      ImGui::Text("Triggers: %s / %s", lt.c_str(), rt.c_str());
      ImGui::Text("Pause / Start: %s", Bind("hulk_bind_start").c_str());
      ImGui::Text("Walk: %s", Bind("hulk_bind_walk").c_str());
      ImGui::Separator();
      const int32_t guest_fps_limit = rex::cvar::Query<int32_t>("gpu_guest_fps_limit");
      if (guest_fps_limit == 0) {
        ImGui::TextUnformatted("Guest FPS limit: Unlimited (game may still run 30)");
      } else {
        ImGui::Text("Guest FPS limit: %d", guest_fps_limit);
      }
      if (ImGui::Button("30 FPS (console physics)")) {
        (void)rex::cvar::SetFlagByName("gpu_guest_fps_limit", "30");
      }
      ImGui::SameLine();
      if (ImGui::Button("60 FPS (experimental)")) {
        (void)rex::cvar::SetFlagByName("gpu_guest_fps_limit", "60");
      }
      // HULK/FPS-CHOICES: the guest frame limiter is separate from Vulkan
      // presentation. Do NOT alter guest clock / physics timestep here:
      // the game's PPC simulation still needs a verified 30Hz decoupling fix.
      ImGui::TextUnformatted("Higher frame rates (experimental physics)");
      if (ImGui::Button("90 FPS")) {
        (void)rex::cvar::SetFlagByName("gpu_guest_fps_limit", "90");
      }
      ImGui::SameLine();
      if (ImGui::Button("120 FPS")) {
        (void)rex::cvar::SetFlagByName("gpu_guest_fps_limit", "120");
      }
      ImGui::SameLine();
      if (ImGui::Button("144 FPS")) {
        (void)rex::cvar::SetFlagByName("gpu_guest_fps_limit", "144");
      }
      ImGui::SameLine();
      if (ImGui::Button("Unlimited FPS")) {
        (void)rex::cvar::SetFlagByName("gpu_guest_fps_limit", "0");
      }
      bool vsync = rex::cvar::Query<bool>("vsync");
      if (ImGui::Checkbox("Vertical synchronization", &vsync)) {
        (void)rex::cvar::SetFlagByName("vsync", vsync ? "true" : "false");
      }
      ImGui::TextWrapped(
          "30 FPS matches the console update cadence. At 60+ FPS some "
          "animations and rigid-body interactions can still run incorrectly. "
          "Selecting a higher FPS does not change the physics timestep.");
      ImGui::TextUnformatted("Use F4 Settings > Save to persist changed FPS.");
      // HULK/PC-GRAPHICS: these NativeX settings are read at startup.
      // Expose only renderer-supported paths; do not claim DLSS works
      // before a verified main-scene view and motion vectors are available.
      ImGui::Separator();
      ImGui::TextUnformatted("PC graphics (restart game to apply)");
      const double native_scale =
          rex::cvar::Query<double>("native_x_renderer_resolution_scale");
      ImGui::Text("Native rendering scale: %.2fx", native_scale);
      if (ImGui::Button("Rendering x1")) {
        (void)rex::cvar::SetFlagByName("native_x_renderer_resolution_scale", "1.0");
      }
      ImGui::SameLine();
      if (ImGui::Button("Rendering x2")) {
        (void)rex::cvar::SetFlagByName("native_x_renderer_resolution_scale", "2.0");
      }
      const std::string scaling = Bind("native_x_renderer_scaling");
      ImGui::Text("Presentation filter: %s", scaling.c_str());
      if (ImGui::Button("Bilinear + CAS")) {
        (void)rex::cvar::SetFlagByName("native_x_renderer_scaling", "bilinear");
      }
      ImGui::SameLine();
      if (ImGui::Button("FSR 1")) {
        (void)rex::cvar::SetFlagByName("native_x_renderer_scaling", "fsr");
      }
      ImGui::SameLine();
      if (ImGui::Button("NIS")) {
        (void)rex::cvar::SetFlagByName("native_x_renderer_scaling", "nis");
      }
      float sharpness = static_cast<float>(
          rex::cvar::Query<double>("native_x_renderer_sharpness"));
      if (sharpness < 0.0f) sharpness = 0.0f;
      if (sharpness > 1.0f) sharpness = 1.0f;
      if (ImGui::SliderFloat("Sharpening", &sharpness, 0.0f, 0.5f, "%.2f")) {
        (void)rex::cvar::SetFlagByName("native_x_renderer_sharpness",
                                       std::to_string(sharpness));
      }
      ImGui::TextUnformatted("Note: sharpen cannot repair missing shader samples.");
      ImGui::TextUnformatted("DLSS/DLAA requires verified scene and motion vectors.");
      ImGui::TextUnformatted("Save in F4 Settings, then restart for graphics changes.");

      ImGui::Separator();
      ImGui::TextUnformatted("F4 - PC settings | F8 - recapture mouse");
      ImGui::TextUnformatted("F10 - toggle PC menu | Alt+F4 - close game");
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
    if (!open) PcMenuSetOpen(false);
  }
 private:
  std::function<void()> on_exit_;
};
} // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreatePcMenu(
    rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
    std::function<void()> on_exit) {
  return std::make_unique<PcMenu>(drawer, window, std::move(on_exit));
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
