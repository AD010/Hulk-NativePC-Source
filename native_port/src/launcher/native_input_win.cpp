// Native Win32 keyboard and raw mouse bridge for the recompiled Hulk's XInput wrapper.
// Opt in with --hulk_native_mnk=true or HULK_NATIVE_MNK=1. F8 releases/captures mouse.
// If a verified camera layout is supplied, mouse deltas update guest yaw/pitch directly.
// Otherwise, the original right-stick fallback remains for playability.
#include "native_input_map.h"
#include "native_camera_direct.h"
#include "pc_menu.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <array>
#include <mutex>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(hulk_native_mnk, false, "Hulk",
                    "Enable keyboard and Raw Input mouse bridge (Xbox analog-stick emulation)");
REXCVAR_DEFINE_BOOL(hulk_native_mnk_force, true, "Hulk",
                    "Native keyboard/mouse has priority over physical controller state");


// ReXGlue's existing F4 Settings overlay exposes these configurable PC bindings.
REXCVAR_DEFINE_DOUBLE(hulk_mouse_sensitivity, 4.0, "Hulk/PC Controls",
                      "Mouse look sensitivity (right stick fallback)").range(0.1, 20.0);
REXCVAR_DEFINE_BOOL(hulk_mouse_invert_y, false, "Hulk/PC Controls",
                    "Invert vertical mouse motion");
REXCVAR_DEFINE_STRING(hulk_bind_forward, "W", "Hulk/PC Controls/Keys", "Move forward");
REXCVAR_DEFINE_STRING(hulk_bind_left, "A", "Hulk/PC Controls/Keys", "Move left");
REXCVAR_DEFINE_STRING(hulk_bind_backward, "S", "Hulk/PC Controls/Keys", "Move back");
REXCVAR_DEFINE_STRING(hulk_bind_right, "D", "Hulk/PC Controls/Keys", "Move right");
REXCVAR_DEFINE_STRING(hulk_bind_menu_up, "Up", "Hulk/PC Controls/Keys", "Menu up");
REXCVAR_DEFINE_STRING(hulk_bind_menu_down, "Down", "Hulk/PC Controls/Keys", "Menu down");
REXCVAR_DEFINE_STRING(hulk_bind_menu_left, "Left", "Hulk/PC Controls/Keys", "Menu left");
REXCVAR_DEFINE_STRING(hulk_bind_menu_right, "Right", "Hulk/PC Controls/Keys", "Menu right");
REXCVAR_DEFINE_STRING(hulk_bind_confirm, "Space", "Hulk/PC Controls/Keys", "Xbox A (confirm)");
REXCVAR_DEFINE_STRING(hulk_bind_back, "E", "Hulk/PC Controls/Keys", "Xbox B (back)");
REXCVAR_DEFINE_STRING(hulk_bind_primary, "MouseLeft", "Hulk/PC Controls/Keys", "Xbox X");
REXCVAR_DEFINE_STRING(hulk_bind_secondary, "F", "Hulk/PC Controls/Keys", "Xbox Y");
REXCVAR_DEFINE_STRING(hulk_bind_alt_back, "MouseRight", "Hulk/PC Controls/Keys", "Xbox B alternate");
REXCVAR_DEFINE_STRING(hulk_bind_lb, "Q", "Hulk/PC Controls/Keys", "Xbox LB");
REXCVAR_DEFINE_STRING(hulk_bind_rb, "R", "Hulk/PC Controls/Keys", "Xbox RB");
REXCVAR_DEFINE_STRING(hulk_bind_lt, "Shift", "Hulk/PC Controls/Keys", "Left trigger");
REXCVAR_DEFINE_STRING(hulk_bind_rt, "Ctrl", "Hulk/PC Controls/Keys", "Right trigger");
REXCVAR_DEFINE_STRING(hulk_bind_start, "Enter", "Hulk/PC Controls/Keys", "Xbox Start");
REXCVAR_DEFINE_STRING(hulk_bind_select, "Tab", "Hulk/PC Controls/Keys", "Xbox Back");
REXCVAR_DEFINE_STRING(hulk_bind_rstick_click, "MouseMiddle", "Hulk/PC Controls/Keys", "Right stick press");

// Wheel-driven walking like SCDA-XBERecomp. These values control actual analog
// XInput L-stick amplitude, not a timer or artificial animation multiplier.
REXCVAR_DEFINE_BOOL(hulk_wheel_adjust_speed, true, "Hulk/PC Controls/Movement",
                    "Wheel changes walking/running speed; off: wheel sends LB/RB");
REXCVAR_DEFINE_INT32(hulk_move_speed_level, 4, "Hulk/PC Controls/Movement",
                     "WASD movement speed: 0 walking through 4 full run").range(0, 4);
REXCVAR_DEFINE_DOUBLE(hulk_walk_min_strength, 0.36, "Hulk/PC Controls/Movement",
                      "Analog left-stick strength at walking level (adjust for game thresholds)")
    .range(0.26, 0.95);
REXCVAR_DEFINE_STRING(hulk_bind_walk, "C", "Hulk/PC Controls/Movement",
                      "Hold to walk slowly without changing saved speed");
REXCVAR_DEFINE_BOOL(hulk_mouse_diagnostics, true, "Hulk/PC Controls/Diagnostics",
                    "Report Win32 Raw Input and mapped stick motion every 5 seconds");

namespace hulk {
namespace {
std::atomic<bool> g_capture{true};
std::atomic<int32_t> g_dx{0}, g_dy{0};
std::atomic<uint32_t> g_raw_messages{0}, g_stick_motion_polls{0};
std::atomic<uint64_t> g_raw_abs_x{0}, g_raw_abs_y{0};
std::atomic<uint64_t> g_last_report{0};
std::atomic<int32_t> g_held_dx{0}, g_held_dy{0};
std::atomic<uint64_t> g_mouse_hold_until{0};
std::atomic<int> g_pending_wheel{0}, g_wheel_dir{0};
std::atomic<uint64_t> g_wheel_hold_until{0};
std::atomic<uint32_t> g_packet{0};
std::mutex g_state_mutex;
std::array<uint8_t, 12> g_last_pad{};
bool g_have_last_pad = false;
#if defined(_WIN32)
std::atomic<HWND> g_game_window{nullptr};
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool BindTokenDown(std::string_view token) {
  while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
    token.remove_prefix(1);
  while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
    token.remove_suffix(1);
  if (token.empty()) return false;
  if (token.size() == 1) {
    const char ch = char(std::toupper(static_cast<unsigned char>(token.front())));
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return Down(ch);
  }
  const auto is = [&](std::string_view key) {
    if (key.size() != token.size()) return false;
    for (size_t i = 0; i < key.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(key[i])) !=
          std::tolower(static_cast<unsigned char>(token[i]))) return false;
    }
    return true;
  };
  if (is("Space")) return Down(VK_SPACE);
  if (is("Enter") || is("Return")) return Down(VK_RETURN);
  if (is("Tab")) return Down(VK_TAB);
  if (is("Shift")) return Down(VK_SHIFT);
  if (is("Ctrl") || is("Control")) return Down(VK_CONTROL);
  if (is("Alt")) return Down(VK_MENU);
  if (is("Esc") || is("Escape")) return Down(VK_ESCAPE);
  if (is("Up")) return Down(VK_UP);
  if (is("Down")) return Down(VK_DOWN);
  if (is("Left")) return Down(VK_LEFT);
  if (is("Right")) return Down(VK_RIGHT);
  if (is("MouseLeft")) return Down(VK_LBUTTON);
  if (is("MouseRight")) return Down(VK_RBUTTON);
  if (is("MouseMiddle")) return Down(VK_MBUTTON);
  return false;
}
bool BindDown(std::string_view names) {
  while (!names.empty()) {
    const auto pos = names.find(',');
    if (BindTokenDown(names.substr(0, pos))) return true;
    if (pos == std::string_view::npos) break;
    names.remove_prefix(pos + 1);
  }
  return false;
}
#endif
[[maybe_unused]] bool EnvEnabled() {
  const char* p = std::getenv("HULK_NATIVE_MNK");
  return p && (*p == '1' || *p == 'y' || *p == 'Y');
}
[[maybe_unused]] bool DirectMouseOnly() {
  const char* v = std::getenv("HULK_NATIVE_MOUSE_DIRECT_ONLY");
  return v && v[0] == '1';
}
[[maybe_unused]] float Sensitivity() {
  const char* p = std::getenv("HULK_NATIVE_MOUSE_SENSITIVITY");
  const float configured = static_cast<float>(REXCVAR_GET(hulk_mouse_sensitivity));
  const float fallback = std::isfinite(configured) ? std::clamp(configured, 0.1f, 20.0f) : 4.0f;
  if (!p) return fallback;
  char* end = nullptr;
  const float value = std::strtof(p, &end);
  return end == p || !std::isfinite(value) ? fallback : std::clamp(value, 0.1f, 20.f);
}
void StorePacket(uint8_t* state, uint32_t packet) {
  state[0] = uint8_t(packet >> 24);
  state[1] = uint8_t(packet >> 16);
  state[2] = uint8_t(packet >> 8);
  state[3] = uint8_t(packet);
}
}
bool NativeInputEnabled() {
#if defined(_WIN32)
  return EnvEnabled() || REXCVAR_GET(hulk_native_mnk);
#else
  return false;
#endif
}
void NativeInputAttachWindow(void* window) {
#if defined(_WIN32)
  HWND hwnd = static_cast<HWND>(window);
  if (!hwnd || !NativeInputEnabled()) return;
  g_game_window = hwnd;
  RAWINPUTDEVICE device{};
  device.usUsagePage = 0x01;
  device.usUsage = 0x02; // mouse
  device.dwFlags = 0; // foreground only; retain legacy messages for SDL/other UI
  device.hwndTarget = hwnd;
  const bool ok = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
  if (ok) {
    REXLOG_INFO("[HULK/INPUT] Win32 RawInput mouse registered on game window");
  } else {
    REXLOG_ERROR("[HULK/INPUT] Win32 RawInput mouse registration FAILED: Win32 error {}",
                 GetLastError());
  }
#else
  (void)window;
#endif
}
void NativeInputSetCapture(bool enabled) {
  g_capture.store(enabled);
  g_dx = g_dy = 0;
  g_held_dx = g_held_dy = 0;
  g_mouse_hold_until = 0;
  g_pending_wheel = g_wheel_dir = 0;
  g_wheel_hold_until = 0;
}
void NativeInputToggleCapture() { NativeInputSetCapture(!g_capture.load()); }
void NativeInputReleaseCapture() { NativeInputSetCapture(false); }
bool NativeInputCapturing() { return g_capture && NativeInputEnabled(); }
void NativeInputWheel(int steps) {
  if (!NativeInputCapturing() || !steps) return;
  // Positive scroll = LB, negative = RB; keep the pulse long enough to
  // survive multiple XInput polls before the game updates its controls.
  // WM_MOUSEWHEEL may deliver multiple notches before the next game poll.
  // Preserve their sum instead of losing fast wheel motion.
  g_pending_wheel.fetch_add(std::clamp(steps, -10, 10));
}
void NativeInputRawMouse(int dx, int dy) {
  if (!NativeInputCapturing()) return;
  const int x = std::clamp(dx,-2000,2000);
  const int y = std::clamp(dy,-2000,2000);
  g_dx.fetch_add(x, std::memory_order_relaxed);
  g_dy.fetch_add(y, std::memory_order_relaxed);
  g_raw_messages.fetch_add(1, std::memory_order_relaxed);
  g_raw_abs_x.fetch_add(static_cast<uint64_t>(std::abs(x)), std::memory_order_relaxed);
  g_raw_abs_y.fetch_add(static_cast<uint64_t>(std::abs(y)), std::memory_order_relaxed);
}
// Called after the real Xam pad read; returns true when a virtual pad was supplied.
// Does not override a connected physical controller.
bool NativeInputApply(uint32_t user, bool real_pad_connected, uint8_t* state, uint8_t* guest_base) {
  if (user != 0 || !NativeInputEnabled() || !state) return false;
  // The SDK can expose its own keyboard-as-XInput device (mnk_mode=true). In that case
  // Xam reports a connected pad and the previous bridge silently did nothing.
  // Do not override a physical controller unless the user opts in explicitly.
  if (real_pad_connected && !REXCVAR_GET(hulk_native_mnk_force)) return false;
  input::Frame f{};
#if defined(_WIN32)
  HWND hwnd = g_game_window;
  const bool focused = hwnd && GetForegroundWindow() == hwnd;
  // Neutral Xbox state while the PC host menu captures keyboard/mouse;
  // gameplay input is untouched once the menu closes.
  if (focused && !PcMenuIsOpen()) {
    const auto add = [&](std::string_view name, uint32_t bit) {
      if (BindDown(name)) f.keys |= bit;
    };
    add(REXCVAR_GET(hulk_bind_forward), input::W);
    add(REXCVAR_GET(hulk_bind_left), input::A);
    add(REXCVAR_GET(hulk_bind_backward), input::S);
    add(REXCVAR_GET(hulk_bind_right), input::D);
    add(REXCVAR_GET(hulk_bind_menu_up), input::Up);
    add(REXCVAR_GET(hulk_bind_menu_down), input::Down);
    add(REXCVAR_GET(hulk_bind_menu_left), input::Left);
    add(REXCVAR_GET(hulk_bind_menu_right), input::Right);
    add(REXCVAR_GET(hulk_bind_confirm), input::Space);
    add(REXCVAR_GET(hulk_bind_back), input::E);
    add(REXCVAR_GET(hulk_bind_secondary), input::F);
    add(REXCVAR_GET(hulk_bind_lb), input::Q);
    add(REXCVAR_GET(hulk_bind_rb), input::R);
    add(REXCVAR_GET(hulk_bind_lt), input::Shift);
    add(REXCVAR_GET(hulk_bind_rt), input::Ctrl);
    add(REXCVAR_GET(hulk_bind_start), input::Enter);
    add(REXCVAR_GET(hulk_bind_select), input::Tab);
    // F8 releases mouse capture, not keyboard movement or menu controls.
    if (g_capture) {
      add(REXCVAR_GET(hulk_bind_primary), input::MouseLeft);
      add(REXCVAR_GET(hulk_bind_alt_back), input::MouseRight);
      add(REXCVAR_GET(hulk_bind_rstick_click), input::MouseMiddle);
      f.mouse_dx = g_dx.exchange(0);
      f.mouse_dy = g_dy.exchange(0);
      if (REXCVAR_GET(hulk_mouse_invert_y)) f.mouse_dy = -f.mouse_dy;
      const auto now = static_cast<uint64_t>(GetTickCount64());
      const int wheel = g_pending_wheel.exchange(0);
      if (REXCVAR_GET(hulk_wheel_adjust_speed)) {
        g_wheel_hold_until.store(0);
        if (wheel) {
          const int current = std::clamp(REXCVAR_GET(hulk_move_speed_level), 0, 4);
          const int updated = std::clamp(current + wheel, 0, 4);
          if (updated != current) {
            REXCVAR_SET(hulk_move_speed_level, updated);
            REXLOG_INFO("[HULK/INPUT] Wheel speed level {} / 4, walking stick strength {:.3f}",
                        updated, input::MovementStrength(
                            updated, static_cast<float>(REXCVAR_GET(hulk_walk_min_strength))));
          }
          // Refresh the stick this same poll, not only from the next tick.
          const bool walking = BindDown(REXCVAR_GET(hulk_bind_walk));
          f.move_stick_strength = input::MovementStrength(
              walking ? 0 : updated, static_cast<float>(REXCVAR_GET(hulk_walk_min_strength)));
        }
      } else {
        // Compatibility option for LB/RB controls in the game; 90-ms pulse.
        if (wheel) {
          g_wheel_dir.store(wheel);
          g_wheel_hold_until.store(now + 90);
        }
        if (now < g_wheel_hold_until.load()) {
          if (g_wheel_dir.load() > 0) f.keys |= input::Q;
          if (g_wheel_dir.load() < 0) f.keys |= input::R;
        }
      }
    } else {
      g_dx = 0;
      g_dy = 0;
    }
    f.sensitivity = Sensitivity();
    // Walk modifier temporarily selects level 0; wheel controls the persistent
    // five-step level. Xbox left-stick remains the game's only movement input.
    const bool walking = BindDown(REXCVAR_GET(hulk_bind_walk));
    const float min = static_cast<float>(REXCVAR_GET(hulk_walk_min_strength));
    const int level = std::clamp(REXCVAR_GET(hulk_move_speed_level), 0, 4);
    f.move_stick_strength = input::MovementStrength(walking ? 0 : level, min);
  } else {
    g_dx = 0;
    g_dy = 0;
  }
#endif
  // In a *validated* direct-camera profile the mouse writes yaw/pitch in guest
  // camera state; zero the Xbox right stick so it cannot double-rotate.
  // No profile => keep the old input path unchanged for playability.
  const bool direct_camera = camera::ApplyToGameMemory(
      guest_base, f.mouse_dx, f.mouse_dy);
#if defined(_WIN32)
  // A game can poll XInput many times between rendered frames. Preserve small
  // mouse movements briefly *only for stick emulation*, not direct camera:
  // repeating guest yaw/pitch writes would over-rotate the native camera.
  if (!direct_camera && g_capture.load() && focused) {
    const auto now = static_cast<uint64_t>(GetTickCount64());
    if (f.mouse_dx || f.mouse_dy) {
      g_held_dx.store(f.mouse_dx);
      g_held_dy.store(f.mouse_dy);
      g_mouse_hold_until.store(now + 25);
    } else if (now < g_mouse_hold_until.load()) {
      f.mouse_dx = g_held_dx.load();
      f.mouse_dy = g_held_dy.load();
    }
  } else {
    g_mouse_hold_until.store(0);
  }
#endif
  f.mouse_as_right_stick = !direct_camera && !DirectMouseOnly();
  // Writes the actual big-endian XINPUT_STATE game buffer, not host SDL state.
  input::ApplyFrame(f, state);
#if defined(_WIN32)
  if (REXCVAR_GET(hulk_mouse_diagnostics)) {
    if (f.mouse_dx || f.mouse_dy) {
      g_stick_motion_polls.fetch_add(1, std::memory_order_relaxed);
    }
    const auto now = static_cast<uint64_t>(GetTickCount64());
    auto last = g_last_report.load(std::memory_order_relaxed);
    if (now > last + 5000 &&
        g_last_report.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
      const int16_t rx = static_cast<int16_t>((uint16_t(state[12]) << 8) | state[13]);
      const int16_t ry = static_cast<int16_t>((uint16_t(state[14]) << 8) | state[15]);
      REXLOG_INFO("[HULK/INPUT] Win32 RawInput: messages={} |dx|={} |dy|={}, "
                  "mapped mouse polls={}, focused={}, capture={}, "
                  "XInput right-stick rx={} ry={}, left-stick strength={:.3f}",
                  g_raw_messages.exchange(0), g_raw_abs_x.exchange(0), g_raw_abs_y.exchange(0),
                  g_stick_motion_polls.exchange(0), focused, g_capture.load(),
                  rx, ry, f.move_stick_strength);
    }
  }
#endif
  {
    // XInput's packet number only advances when the game-visible buttons,
    // triggers or sticks change. Poll frequency must not manufacture inputs.
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (!g_have_last_pad ||
        std::memcmp(g_last_pad.data(), state + 4, g_last_pad.size()) != 0) {
      std::memcpy(g_last_pad.data(), state + 4, g_last_pad.size());
      g_have_last_pad = true;
      ++g_packet;
    }
    StorePacket(state, g_packet.load());
  }
  return true;
}
} // namespace hulk
