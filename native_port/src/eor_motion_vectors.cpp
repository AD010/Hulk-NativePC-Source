#include "eor_motion_vectors.h"
#include <cmath>
#include <limits>
namespace hulk::eor::temporal {
namespace {
bool Finite(Clip v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) &&
         std::isfinite(v.z) && std::isfinite(v.w);
}
bool Ready(const FrameTag& t) noexcept {
  return t.width > 0 && t.height > 0 && t.width <= 16384 && t.height <= 16384 &&
         t.geometry_motion_verified && t.scene_depth_verified &&
         t.camera_abi_verified && !t.camera_cut;
}
} // namespace
void History::Invalidate() noexcept { seeded_ = valid_ = false; last_ = {}; }
bool History::Begin(const FrameTag& tag) noexcept {
  const bool current_ok = Ready(tag);
  const bool successive = seeded_ && tag.frame > 0 &&
      last_.frame < std::numeric_limits<uint64_t>::max() &&
      tag.frame == last_.frame + 1 && last_.width == tag.width &&
      last_.height == tag.height && last_.camera_epoch == tag.camera_epoch &&
      last_.projection_epoch == tag.projection_epoch;
  valid_ = current_ok && successive;
  seeded_ = current_ok;
  last_ = tag;
  return valid_;
}
std::optional<Velocity> FromClip(Clip now, Clip prev, uint32_t width,
                                uint32_t height, PixelJitter nj,
                                PixelJitter pj) noexcept {
  if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
      !Finite(now) || !Finite(prev) || now.w <= 1e-9 || prev.w <= 1e-9 ||
      !std::isfinite(nj.x) || !std::isfinite(nj.y) ||
      !std::isfinite(pj.x) || !std::isfinite(pj.y)) return std::nullopt;
  const double now_depth = now.z / now.w, prev_depth = prev.z / prev.w;
  if (!(now_depth >= 0.0 && now_depth <= 1.0 &&
        prev_depth >= 0.0 && prev_depth <= 1.0)) return std::nullopt;
  const double vx = 0.5 * double(width) * (now.x / now.w - prev.x / prev.w)
                    - (nj.x - pj.x);
  const double vy = -0.5 * double(height) * (now.y / now.w - prev.y / prev.w)
                    - (nj.y - pj.y);
  if (!std::isfinite(vx) || !std::isfinite(vy)) return std::nullopt;
  return Velocity{vx, vy};
}
} // namespace hulk::eor::temporal
