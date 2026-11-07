// The game's controller reads, and what TRG Launcher 2.0 needs from them.
//
// sub_828AA818 is the title's XInputGetState (r3 user, r4 XINPUT_STATE): it calls XamInputGetState(user, 1, state).
// It is the only wrapper the game calls for pad state (from three places). The other Xam input wrappers:
//   sub_828AA808  XInputGetCapabilities
//   sub_828AA828  a capabilities check that also calls XamInputSetState
//   sub_828AA8F0  XInputSetState (vibration)
//   sub_828AA908, sub_828AA928  XInputGetKeystrokeEx
// (sub_828AA810 is an XInputGetStateEx the game never calls.)
//
// Every Xam input call goes through one lock: the runtime's InputSystem is not safe to call from two threads at
// once (Big Bumpin' crashed that way). After each read:
//   * a failed read leaves a zeroed state, as on the console (Doritos Crash Course: stale stack bytes became held
//     buttons when the runtime left the buffer untouched);
//   * return_to_launcher.cpp watches Back and Start, and Esc;
//   * game_window.cpp brings the window forward once and hides the cursor while the pad is used.
//
// XINPUT_STATE (big endian): +0 packet number, +4 buttons, +6 left trigger, +7 right trigger, +8/+10 left stick,
// +12/+14 right stick.
//
// Developer aid, not in release builds (HULK_DEV_TOOLS): HULK_DEV_PAD scripts controller 1 for test runs, with no
// real pad and without the window in front.
// A list of <button>@<seconds>, counted from the first pad read, each held for a fifth of a second:
//   HULK_DEV_PAD=start@75,a@80,down@83,a@85
// Buttons: a b x y lb rb start back up down left right ls rs. Slot 0 then reads as connected.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <rex/hook.h>

namespace hulk {

void ReturnToLauncherTick(uint32_t user, bool connected, uint16_t buttons);  // return_to_launcher.cpp
void GameWindowTick(bool pad_used);                                          // game_window.cpp
bool PadChanged(uint32_t user, const uint8_t* state16);                      // game_window.cpp
bool NativeInputEnabled();
bool NativeInputApply(uint32_t user, bool real_pad_connected, uint8_t* state, uint8_t* guest_base);

namespace {
std::mutex xam_input_mutex;
using XamInputLock = std::lock_guard<std::mutex>;
uint16_t Load16(const uint8_t* base, uint32_t a) { return uint16_t(base[a] << 8 | base[a + 1]); }
void Store16(uint8_t* base, uint32_t a, uint16_t v) {
  base[a] = uint8_t(v >> 8);
  base[a + 1] = uint8_t(v);
}

struct DevPress {
  uint16_t mask;
  double at;
};

uint16_t ButtonMask(const std::string& name) {
  static const struct { const char* name; uint16_t mask; } kButtons[] = {
      {"up", 0x0001},    {"down", 0x0002}, {"left", 0x0004}, {"right", 0x0008}, {"start", 0x0010},
      {"back", 0x0020},  {"ls", 0x0040},   {"rs", 0x0080},   {"lb", 0x0100},    {"rb", 0x0200},
      {"a", 0x1000},     {"b", 0x2000},    {"x", 0x4000},    {"y", 0x8000}};
  for (const auto& b : kButtons)
    if (name == b.name) return b.mask;
  return 0;
}

const std::vector<DevPress>* DevPad() {
  static const std::vector<DevPress>* script = [] () -> const std::vector<DevPress>* {
#if !defined(HULK_DEV_TOOLS)
    return nullptr;  // release builds: no developer pad
#endif
    const char* env = std::getenv("HULK_DEV_PAD");
    if (!env || !*env) return nullptr;
    auto* out = new std::vector<DevPress>;
    std::string s(env);
    size_t start = 0;
    while (start < s.size()) {
      size_t end = s.find(',', start);
      if (end == std::string::npos) end = s.size();
      const std::string item = s.substr(start, end - start);
      const size_t at = item.find('@');
      if (at != std::string::npos) out->push_back({ButtonMask(item.substr(0, at)), std::atof(item.c_str() + at + 1)});
      start = end + 1;
    }
    return out;
  }();
  return script;
}

// The scripted buttons held now, counted from the first call.
uint16_t DevButtons() {
  static const auto t0 = std::chrono::steady_clock::now();
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  uint16_t held = 0;
  for (const DevPress& p : *DevPad())
    if (now >= p.at && now < p.at + 0.2) held |= p.mask;
  return held;
}
}  // namespace

}  // namespace hulk

REX_EXTERN(__imp__sub_828AA818);
REX_HOOK_RAW(sub_828AA818) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  {
    hulk::XamInputLock lock(hulk::xam_input_mutex);
    __imp__sub_828AA818(ctx, base);
  }
  bool connected = ctx.r3.u32 == 0 && state;
  if (!connected && state) std::memset(base + state, 0, 16);
  if (user == 0 && state && hulk::NativeInputApply(user, connected, base + state, base)) {
    ctx.r3.u64 = 0;  // virtual XInput slot 0 exposed to the guest
    connected = true;
  }
  if (user == 0 && state && hulk::DevPad()) {
    static uint32_t packet = 0;
    if (!connected) {
      ctx.r3.u64 = 0;  // ERROR_SUCCESS: a pad is there
      connected = true;
    }
    const uint16_t buttons = hulk::Load16(base, state + 4) | hulk::DevButtons();
    hulk::Store16(base, state + 4, buttons);
    ++packet;
    hulk::Store16(base, state, uint16_t(packet >> 16));
    hulk::Store16(base, state + 2, uint16_t(packet));
  }
  hulk::ReturnToLauncherTick(user, connected, connected ? hulk::Load16(base, state + 4) : 0);
  hulk::GameWindowTick(connected && hulk::PadChanged(user, base + state));
}

#define HULK_LOCKED_INPUT(fn)                         \
  REX_EXTERN(__imp__##fn);                            \
  REX_HOOK_RAW(fn) {                                  \
    hulk::XamInputLock lock(hulk::xam_input_mutex);   \
    __imp__##fn(ctx, base);                           \
  }

// XInputGetCapabilities (r3 user, r4 flags, r5 XINPUT_CAPABILITIES): with the developer pad, slot 0 is a gamepad.
REX_EXTERN(__imp__sub_828AA808);
REX_HOOK_RAW(sub_828AA808) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t caps = ctx.r5.u32;
  {
    hulk::XamInputLock lock(hulk::xam_input_mutex);
    __imp__sub_828AA808(ctx, base);
  }
  if (user == 0 && caps && ctx.r3.u32 != 0 && (hulk::DevPad() || hulk::NativeInputEnabled())) {
    std::memset(base + caps, 0, 20);
    base[caps] = 1;      // XINPUT_DEVTYPE_GAMEPAD
    base[caps + 1] = 1;  // XINPUT_DEVSUBTYPE_GAMEPAD
    ctx.r3.u64 = 0;
  }
}
HULK_LOCKED_INPUT(sub_828AA828)
HULK_LOCKED_INPUT(sub_828AA8F0)
HULK_LOCKED_INPUT(sub_828AA908)
HULK_LOCKED_INPUT(sub_828AA928)
