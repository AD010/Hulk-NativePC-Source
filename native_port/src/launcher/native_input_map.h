// Hulk Native PC: platform-independent keyboard/mouse to Xbox 360 state mapping.
// The guest XINPUT_STATE is 16 bytes, big endian on Xenon.
#pragma once
#include <cstdint>
namespace hulk::input {
enum Key : uint32_t {
  W=1u<<0, A=1u<<1, S=1u<<2, D=1u<<3,
  Up=1u<<4, Down=1u<<5, Left=1u<<6, Right=1u<<7,
  Space=1u<<8, E=1u<<9, F=1u<<10, Q=1u<<11, R=1u<<12,
  Shift=1u<<13, Ctrl=1u<<14, Tab=1u<<15, Enter=1u<<16,
  MouseLeft=1u<<17, MouseRight=1u<<18, MouseMiddle=1u<<19
};
struct Frame {
  uint32_t keys = 0;
  int32_t mouse_dx = 0, mouse_dy = 0;
  float sensitivity = 4.0f;
  // Keyboard WASD sends a partial Xbox 360 left-stick tilt, not a binary
  // digital movement command. Physical pads retain their original analog input.
  float move_stick_strength = 28000.0f / 32767.0f;
  // Direct camera mode writes guest yaw/pitch separately and leaves right stick neutral.
  // A right-stick fallback remains opt-in while no validated camera layout exists.
  bool mouse_as_right_stick = true;
};
// Writes only buttons, triggers and sticks at offsets +4..+15; packet number is caller-owned.
// Does not access guest memory outside the 16-byte state. Pure and independently testable.
// Five user-selected walking/running levels, calculated without guest memory.
float MovementStrength(int level, float min_strength);
void ApplyFrame(const Frame& frame, uint8_t state[16]);
}
