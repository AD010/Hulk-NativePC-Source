// Hulk Direct camera binding: host Raw Input deltas -> guest camera angles.
// Separate from XInput. Game-specific memory layout must be validated before enabling.
#pragma once
#include <cstddef>
#include <cstdint>
namespace hulk::camera {
struct Layout {
  uint32_t pointer_slot=0;  // guest address of big-endian camera object pointer
  uint32_t yaw_offset=0;    // big-endian float field in camera object
  uint32_t pitch_offset=0;
  float radians_per_count=0.002f;
  float pitch_min=-1.4f;
  float pitch_max=1.4f;
  bool invert_y=false;
};
struct Angles {float yaw=0,pitch=0;};
Angles ApplyDelta(Angles angles,int dx,int dy,const Layout& cfg);
// Pure bounds-checked guest-address model, used by tests and actual camera bind.
// The span begins at emulated guest address zero; no endianness assumptions.
bool ApplyToGuestSpan(uint8_t* memory,size_t size,const Layout& cfg,int dx,int dy);
// One-time optional sidecar config; fails closed if the required fields are missing.
// A false result means RawInput still follows existing XInput path.
bool ApplyToGameMemory(uint8_t* base,int dx,int dy);
} // namespace hulk::camera
