#pragma once
// Temporal native Vulkan contract. Real previous transforms and camera/depth
// are mandatory: no manufactured zero velocities for DLSS/FSR/FG.
#include <cstdint>
#include <optional>
namespace hulk::eor::temporal {
struct Clip { double x = 0, y = 0, z = 0, w = 0; };
struct PixelJitter { double x = 0, y = 0; };
struct Velocity { double x = 0, y = 0; }; // CURRENT minus PREVIOUS pixels, Y-down
struct FrameTag {
  uint64_t frame = 0, camera_epoch = 0, projection_epoch = 0;
  uint32_t width = 0, height = 0;
  bool geometry_motion_verified = false, scene_depth_verified = false;
  bool camera_abi_verified = false, camera_cut = false;
};
class History {
 public:
  bool Begin(const FrameTag& tag) noexcept;
  void Invalidate() noexcept;
  bool Valid() const noexcept { return valid_; }
 private:
  FrameTag last_{};
  bool seeded_ = false, valid_ = false;
};
// Coordinates are clip-space, with *native Vulkan* depth in [0,1].
std::optional<Velocity> FromClip(Clip current, Clip previous,
                                 uint32_t width, uint32_t height,
                                 PixelJitter current_jitter = {},
                                 PixelJitter previous_jitter = {}) noexcept;
} // namespace hulk::eor::temporal
