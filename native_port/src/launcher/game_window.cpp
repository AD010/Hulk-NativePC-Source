// The game's window:
//  * started by the TRG launcher, it comes to the front over anything open (the launcher allows it with
//    AllowSetForegroundWindow before it steps aside);
//  * the mouse cursor hides while the controller is used and comes back when the mouse moves.
// The window is SDL's; its window procedure is wrapped to answer WM_SETCURSOR while the cursor is hidden.
// Driven from the XInputGetState hook in input_hook.cpp, which the game polls many times a second.
// HULK_TEST_BACKGROUND=1 (test runs) leaves the window where it opens. On Linux this does nothing: gamescope and the
// desktop bring a new window forward themselves.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <array>

#include "pc_menu.h"
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DECLARE(bool, hulk_from_launcher);  // return_to_launcher.cpp

namespace hulk {
void NativeInputAttachWindow(void* window);
void NativeInputToggleCapture();
void NativeInputSetCapture(bool enabled);
void NativeInputReleaseCapture();
bool NativeInputEnabled();
bool NativeInputCapturing();
void NativeInputWheel(int steps);
void NativeInputRawMouse(int dx, int dy);
#if defined(_WIN32)
namespace {

std::atomic<HWND> g_window{nullptr};
std::atomic<WNDPROC> g_old_proc{nullptr};
std::atomic<bool> g_hidden{false};
// Restore mouse capture only if it was active before Alt+Tab or PC menu.
std::atomic<bool> g_restore_after_focus{false};
std::atomic<bool> g_restore_after_menu{false};

POINT g_last_mouse{-100000, -100000};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  // HULK/WINDOW-FOCUS-RECAPTURE: never accumulate RawInput while Windows
  // owns focus (Alt+Tab, Print Screen tools, minimize). Restore the exact
  // prior capture state, not a forced mouse lock on every return.
  if (msg == WM_ACTIVATEAPP) {
    if (!wp) {
      g_restore_after_focus.store(NativeInputCapturing(), std::memory_order_release);
      NativeInputReleaseCapture();
      g_hidden = false;
      g_last_mouse = POINT{-100000, -100000};
    } else if (g_restore_after_focus.exchange(false, std::memory_order_acq_rel) &&
               !PcMenuIsOpen()) {
      NativeInputSetCapture(true);
    }
  }
  if (msg == WM_DESTROY) {
    NativeInputReleaseCapture();
    g_restore_after_focus = false;
    g_restore_after_menu = false;
  }
  // F10 host PC menu is not an Xbox guest controller command.
  // It releases RawInput capture so UI mouse clicks work.
  // F10 is a Windows system key, so it may arrive as WM_SYSKEYDOWN.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_F10 &&
      !(lp & (1L << 30))) {
    const bool opening = !PcMenuIsOpen();
    if (opening)
      g_restore_after_menu.store(NativeInputCapturing(), std::memory_order_release);
    PcMenuToggle();
    REXLOG_INFO("[HULK/PCMENU] F10 message={} open={}",
                msg == WM_SYSKEYDOWN ? "WM_SYSKEYDOWN" : "WM_KEYDOWN",
                PcMenuIsOpen());
    if (PcMenuIsOpen()) {
      NativeInputReleaseCapture();
    } else if (g_restore_after_menu.exchange(false, std::memory_order_acq_rel) &&
               GetForegroundWindow() == hwnd) {
      NativeInputSetCapture(true);
    }
    g_hidden = false;
    SetCursor(LoadCursorA(nullptr, IDC_ARROW));
    PostMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd),
                 MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    return 0;
  }
  // Suppress Win32 menu activation when F10 is released.
  if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && wp == VK_F10) return 0;
  // F11 only produces a GPU verification report if the observer is active.
  // Log delivery without swallowing the SDK's own F11 bind.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_F11 &&
      !(lp & (1L << 30))) {
    REXLOG_INFO("[HULK/PCMENU] F11 pressed NativeX={} verify_only={}",
                rex::cvar::Query<bool>("native_x_renderer"),
                rex::cvar::Query<bool>("hulk_native_x_verify_only"));
  }
  if (msg == WM_INPUT && NativeInputCapturing() &&
      GetForegroundWindow() == hwnd) {
    // A mouse RAWINPUT packet has a fixed C ABI size; avoid a heap allocation
    // and a separate size-query syscall for every motion message on SDL's UI
    // thread. Oversize HID packets are not mouse packets and are ignored.
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    const UINT received = GetRawInputData(
        reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
    // Do not read mouse fields from a short or truncated input message.
    if (received != UINT(-1) &&
        received >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) &&
        raw.header.dwType == RIM_TYPEMOUSE &&
        !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
      NativeInputRawMouse(raw.data.mouse.lLastX, raw.data.mouse.lLastY);
    }
  }
  // Native game capture must not hide ReXGlue's F4 settings cursor.
  // F4 opens the host overlay; F8 can recapture after leaving the overlay.
  if (msg == WM_KEYDOWN && !(lp & (1L << 30)) &&
      (wp == VK_F8 || wp == VK_F4)) {
    if (wp == VK_F4) NativeInputReleaseCapture();
    else NativeInputToggleCapture();
    if (!NativeInputCapturing()) {
      g_hidden = false;
      SetCursor(LoadCursorA(nullptr, IDC_ARROW));
    }
    PostMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd),
                 MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
  }
  if (msg == WM_MOUSEWHEEL && NativeInputCapturing()) {
    static int wheel_remainder = 0; // SDL window thread
    wheel_remainder += GET_WHEEL_DELTA_WPARAM(wp);
    const int steps = wheel_remainder / WHEEL_DELTA;
    wheel_remainder -= steps * WHEEL_DELTA;
    if (steps) NativeInputWheel(steps);
  }
  if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
    if (NativeInputCapturing() || (g_hidden && !NativeInputEnabled())) {
      SetCursor(nullptr);
      return TRUE;
    }
    if (NativeInputEnabled()) {
      SetCursor(LoadCursorA(nullptr, IDC_ARROW));
      return TRUE;
    }
  }
  if (msg == WM_MOUSEMOVE) {
    const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (std::abs(p.x - g_last_mouse.x) + std::abs(p.y - g_last_mouse.y) > 2 && g_last_mouse.x > -100000)
      g_hidden = false;  // the mouse is used again: Windows shows the cursor at the next WM_SETCURSOR
    g_last_mouse = p;
  }
  const WNDPROC previous = g_old_proc.load(std::memory_order_acquire);
  return previous ? CallWindowProcW(previous, hwnd, msg, wp, lp)
                  : DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL CALLBACK FindOwnWindow(HWND hwnd, LPARAM out) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
  *reinterpret_cast<HWND*>(out) = hwnd;
  return FALSE;
}

void Attach() {
  HWND hwnd = nullptr;
  EnumWindows(&FindOwnWindow, LPARAM(&hwnd));
  if (!hwnd) return;
  // Publish the existing procedure BEFORE substituting the thunk: Windows
  // may deliver a focus/resize/Alt+Tab message on the UI thread between the
  // SetWindowLongPtrW call and our next instruction. A null old-proc caused
  // an unsafe callback in that race.
  const WNDPROC original = reinterpret_cast<WNDPROC>(
      GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
  if (!original || original == &WindowProc) return;
  g_old_proc.store(original, std::memory_order_release);
  SetLastError(0);
  const auto previous = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
  if (!previous) {
    // Restore bookkeeping when subclass installation fails.
    g_old_proc.store(nullptr, std::memory_order_release);
    return;
  }
  g_old_proc.store(previous, std::memory_order_release);
  g_window = hwnd;
  NativeInputAttachWindow(hwnd);
  static const bool test_background = std::getenv("HULK_TEST_BACKGROUND") != nullptr;
  if (REXCVAR_GET(hulk_from_launcher) && !test_background) {
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
  }
}

}  // namespace

// `pad_used`: a connected controller's buttons or sticks changed since the last poll.
void GameWindowTick(bool pad_used) {
  static uint32_t polls = 0;
  if (!g_window || !IsWindow(g_window)) {
    if (!IsWindow(g_window)) g_window = nullptr;
    if (++polls % 16 == 0) Attach();  // the window appears a little after the game starts polling
    return;
  }
  if (pad_used && !g_hidden &&
      (NativeInputCapturing() || !NativeInputEnabled())) {
    g_hidden = true;
    POINT p;
    HWND hwnd = g_window;
    if (GetCursorPos(&p) && WindowFromPoint(p) == hwnd)  // refresh the cursor now, if it is over the game
      PostMessageW(hwnd, WM_SETCURSOR, WPARAM(hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
  }
}
#else
void GameWindowTick(bool) {}
#endif

// What the input hook passes: the state it saw last for slot `user`, and whether it changed.
bool PadChanged(uint32_t user, const uint8_t* state16) {
  static uint8_t last[4][12];
  static bool have[4];
  if (user >= 4) return false;
  // Buttons, triggers and both sticks (bytes 4..15 of XINPUT_STATE), ignoring small stick noise.
  bool changed = !have[user] || std::memcmp(last[user], state16 + 4, 4) != 0;
  for (int axis = 0; axis < 4 && !changed; ++axis) {
    const int16_t now = int16_t(state16[8 + axis * 2] << 8 | state16[9 + axis * 2]);
    const int16_t was = int16_t(last[user][4 + axis * 2] << 8 | last[user][5 + axis * 2]);
    changed = std::abs(int(now) - int(was)) > 6000;
  }
  std::memcpy(last[user], state16 + 4, 12);
  const bool first = !have[user];
  have[user] = true;
  return changed && !first;
}

}  // namespace hulk
