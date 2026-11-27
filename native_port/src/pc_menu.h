#pragma once
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
namespace rex::ui { class ImGuiDialog; class ImGuiDrawer; class Window; }
namespace hulk {
// The release uses the original EoR settings route. Explicit developer
// opt-in retains the former overlay for recovery without modifying TOML.
inline bool PcDebugOverlayEnabled() noexcept {
  const char* value=std::getenv("HULK_DEBUG_IMGUI_MENU");
  return value && value[0]=='1' && value[1]=='\0';
}
// Host PC menu (F10) independent of the emulated Xbox 360 pause screens.
// Button labels reflect live keyboard bindings, not assumed Xbox glyphs.
std::unique_ptr<rex::ui::ImGuiDialog> CreatePcMenu(
    rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
    const std::filesystem::path& existing_config,
    std::function<void()> on_exit);
bool PcMenuIsOpen();
void PcMenuSetOpen(bool open);
void PcMenuToggle();
}
