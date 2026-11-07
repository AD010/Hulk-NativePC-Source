#include "native_input_map.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace hulk::input {
namespace {
void StoreBE16(uint8_t* data, uint32_t off, uint16_t v) {
  data[off] = static_cast<uint8_t>(v >> 8);
  data[off + 1] = static_cast<uint8_t>(v);
}
int16_t Axis(int direction, float strength) {
  if (!direction) return 0;
  const float safe = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
  const int magnitude = std::clamp(static_cast<int>(std::lround(safe * 32767.0f)), 0, 32767);
  return static_cast<int16_t>(direction < 0 ? -magnitude : magnitude);
}
int16_t MouseAxis(int32_t delta, float sens) {
  // Raw Input counts converted to Xbox analog deflection. Not yet a direct camera hook.
  if (!std::isfinite(sens)) sens = 4.0f;
  // The Xbox 360 right stick has a large deadzone (typically about 8689).
  // Previously one mouse count at sensitivity 4 produced just 320: the
  // camera would never move unless the pointer jumped tens of counts at once.
  if (!delta) return 0;
  const double magnitude = 9600.0 +
      std::abs(static_cast<double>(delta)) * std::clamp(sens, 0.1f, 20.f) * 150.0;
  const double signed_value = std::copysign(std::min(magnitude, 32767.0),
                                           static_cast<double>(delta));
  return static_cast<int16_t>(signed_value);
}
}
float MovementStrength(int level, float min_strength) {
  // The exact walk/jog/run thresholds belong to the Hulk executable.
  // Never force an unverified guest animation state: vary the real analog stick.
  if (!std::isfinite(min_strength)) min_strength = 0.36f;
  min_strength = std::clamp(min_strength, 0.26f, 0.95f);
  const float t = static_cast<float>(std::clamp(level, 0, 4)) / 4.0f;
  return min_strength + (1.0f - min_strength) * t;
}
void ApplyFrame(const Frame& f, uint8_t state[16]) {
  std::memset(state + 4, 0, 12);
  const auto pressed = [&](uint32_t bit){ return (f.keys & bit) != 0; };
  uint16_t buttons = 0;
  // D-pad arrows: suitable for navigating game menus as well as movement with WASD.
  if (pressed(Up)) buttons |= 0x0001;
  if (pressed(Down)) buttons |= 0x0002;
  if (pressed(Left)) buttons |= 0x0004;
  if (pressed(Right)) buttons |= 0x0008;
  if (pressed(Enter)) buttons |= 0x0010; // Start
  if (pressed(Tab)) buttons |= 0x0020;   // Back
  if (pressed(MouseMiddle)) buttons |= 0x0080; // Right stick click
  if (pressed(Q)) buttons |= 0x0100; // LB
  if (pressed(R)) buttons |= 0x0200; // RB
  if (pressed(Space)) buttons |= 0x1000; // A
  if (pressed(E) || pressed(MouseRight)) buttons |= 0x2000; // B
  if (pressed(MouseLeft)) buttons |= 0x4000; // X
  if (pressed(F)) buttons |= 0x8000; // Y
  StoreBE16(state, 4, buttons);
  state[6] = pressed(Shift) ? 255 : 0;
  state[7] = pressed(Ctrl) ? 255 : 0;
  const int x = (pressed(D)?1:0)-(pressed(A)?1:0);
  const int y = (pressed(W)?1:0)-(pressed(S)?1:0);
  StoreBE16(state, 8, static_cast<uint16_t>(Axis(x, f.move_stick_strength)));
  StoreBE16(state, 10, static_cast<uint16_t>(Axis(y, f.move_stick_strength)));
  const auto rx = f.mouse_as_right_stick ? MouseAxis(f.mouse_dx, f.sensitivity) : int16_t(0);
  const auto ry = f.mouse_as_right_stick ? MouseAxis(-f.mouse_dy, f.sensitivity) : int16_t(0);
  StoreBE16(state, 12, static_cast<uint16_t>(rx));
  StoreBE16(state, 14, static_cast<uint16_t>(ry));
}
}
