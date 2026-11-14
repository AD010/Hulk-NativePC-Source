// Exact perspective aspect math for a future verified Hulk USA camera hook.
// Pure C++20, no guessed PPC offsets, no Xenos surface mutation.
// IMPORTANT: not called by the game until a real XEX projection ABI is found.
#pragma once

#include <cmath>
#include <optional>

namespace hulk::camera {

struct ProjectionAspect {
  double aspect = 0.0;
  // Multiply ONLY the true perspective projection's horizontal focal
  // coefficient M00, after identifying its matrix ABI and 3D camera owner.
  double horizontal_focal_multiplier = 1.0;
  double horizontal_fov_radians = 0.0;
  bool host_frame_has_real_3d_extent = false;
};

// Given an existing game projection with fixed vertical field of view,
// preserving the Y coefficient, adjust the X coefficient to a new aspect.
// The 2D HUD, movies, bloom, shadow maps and EDRAM guest descriptors
// must NOT use this operation.
inline std::optional<ProjectionAspect> AspectForView(
    double target_width, double target_height, double base_aspect,
    double vertical_fov_radians) noexcept {
  if (!std::isfinite(target_width) || !std::isfinite(target_height) ||
      !std::isfinite(base_aspect) || !std::isfinite(vertical_fov_radians) ||
      target_width <= 0.0 || target_height <= 0.0 ||
      base_aspect < 0.5 || base_aspect > 4.5 ||
      vertical_fov_radians <= 0.1 || vertical_fov_radians >= 3.0)
    return std::nullopt;

  const double aspect = target_width / target_height;
  if (!std::isfinite(aspect) || aspect < 0.5 || aspect > 4.5)
    return std::nullopt;

  const double focal_multiplier = base_aspect / aspect;
  const double horizontal_fov =
      2.0 * std::atan(std::tan(vertical_fov_radians * 0.5) * aspect);
  if (!std::isfinite(horizontal_fov) || horizontal_fov <= 0.0 ||
      horizontal_fov >= 3.14159265358979323846)
    return std::nullopt;

  return ProjectionAspect{aspect, focal_multiplier, horizontal_fov, true};
}

}  // namespace hulk::camera
