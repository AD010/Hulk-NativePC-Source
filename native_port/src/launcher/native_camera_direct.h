// Hulk Direct camera binding: host Raw Input deltas -> guest camera angles.
// Separate from XInput. Game-specific memory layout must be validated before enabling.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
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
struct OriginalLookInversion {
  bool yaw=false, pitch=false;
  bool operator==(const OriginalLookInversion&) const = default;
};
// Original USA camera options are the same state used by the actual pause
// menu and the gameplay camera. Only the verified two option bytes may be
// changed, and only in response to an explicit PC settings change.
std::optional<OriginalLookInversion> ReadGameLookInversion(uint8_t* base);
bool WriteGameLookInversion(uint8_t* base,OriginalLookInversion expected,
                            OriginalLookInversion desired);
// Original UI changes win on the axis they changed. PC changes on the
// other axis remain independent. At startup retain an existing PC opt-in
// as well as any enabled saved original option.
inline OriginalLookInversion ReconcileLookInversion(
    std::optional<OriginalLookInversion> previous,
    OriginalLookInversion original,OriginalLookInversion pc) noexcept {
  if(!previous)return {original.yaw || pc.yaw,original.pitch || pc.pitch};
  return {original.yaw!=previous->yaw ? original.yaw : pc.yaw,
          original.pitch!=previous->pitch ? original.pitch : pc.pitch};
}
// A validated camera profile uses the same user sensitivity as RawInput;
// scale 1.0 corresponds to the original default sensitivity value 4.0.
Angles ApplyDelta(Angles angles,int dx,int dy,const Layout& cfg,
                  float sensitivity_scale=1.0f,float sensitivity_y_scale=-1.0f,
                  OriginalLookInversion original={});
// Pure bounds-checked guest-address model, used by tests and actual camera bind.
// The span begins at emulated guest address zero; no endianness assumptions.
bool ApplyToGuestSpan(uint8_t* memory,size_t size,const Layout& cfg,int dx,int dy,
                      float sensitivity_scale=1.0f,float sensitivity_y_scale=-1.0f);
// One-time optional sidecar config; fails closed if the required fields are missing.
// A false result means RawInput still follows existing XInput path.
bool ApplyToGameMemory(uint8_t* base,int dx,int dy,
                       float sensitivity_scale=1.0f,float sensitivity_y_scale=-1.0f);
} // namespace hulk::camera
