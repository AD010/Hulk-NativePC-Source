#pragma once
#include <functional>
#include <memory>
namespace rex::ui { class ImGuiDialog; class ImGuiDrawer; class Window; }
namespace hulk {
// Host PC menu (F10) independent of the emulated Xbox 360 pause screens.
// Button labels reflect live keyboard bindings, not assumed Xbox glyphs.
std::unique_ptr<rex::ui::ImGuiDialog> CreatePcMenu(
    rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
    std::function<void()> on_exit);
bool PcMenuIsOpen();
void PcMenuSetOpen(bool open);
void PcMenuToggle();
}
