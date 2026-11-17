// Exact perspective aspect math for a future verified Hulk USA camera hook.
// Pure C++20, no guessed PPC offsets, no Xenos surface mutation.
// IMPORTANT: not called by the game until a real XEX projection ABI is found.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
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
      base_aspect < 0.25 || base_aspect > 8.0 ||
      vertical_fov_radians <= 0.1 || vertical_fov_radians >= 3.0)
    return std::nullopt;

  const double aspect = target_width / target_height;
  if (!std::isfinite(aspect) || aspect < 0.25 || aspect > 8.0)
    return std::nullopt;

  const double focal_multiplier = base_aspect / aspect;
  const double horizontal_fov =
      2.0 * std::atan(std::tan(vertical_fov_radians * 0.5) * aspect);
  if (!std::isfinite(horizontal_fov) || horizontal_fov <= 0.0 ||
      horizontal_fov >= 3.14159265358979323846)
    return std::nullopt;

  return ProjectionAspect{aspect, focal_multiplier, horizontal_fov, true};
}

// Native perspective camera for every host aspect, keeping the original
// console viewport fully visible. At targets WIDER than the source image,
// preserve vertical FOV and expand horizontal visibility (Hor+). At targets
// TALLER/NARROWER, preserve horizontal FOV and expand vertical visibility
// (Vert+). Do not crop the original 16:9 image or scale the HUD.
// Applies to verified main-view projection only, NOT shadow/reflect/2D/movie.
enum class ClipMatrixConvention : uint8_t {
  Unknown=0, ColumnVector=1, RowVector=2
};
struct SourceCameraProof {
  bool original_main_perspective_view=false;
  bool original_horizontal_fov_verified=false;
  bool original_view_projection_abi_verified=false;
  ClipMatrixConvention convention=ClipMatrixConvention::Unknown;
};
struct MainViewAspectScale {
  double clip_x = 1.0;
  double clip_y = 1.0;
};
inline std::optional<MainViewAspectScale> ExpandAspectNoCrop(
    double original_aspect,uint32_t width,uint32_t height) noexcept {
  if(!std::isfinite(original_aspect) ||
     original_aspect<0.25 || original_aspect>8.0 ||
     !width || !height || width>16384 || height>16384)
    return std::nullopt;
  const double target=double(width)/double(height);
  if(!std::isfinite(target) || target<0.25 || target>8.0)
    return std::nullopt;
  // For ultrawide use Hor+, for 4:3/16:10/portrait use Vert+.
  // Exactly one clip axis may scale down, never both and never above 1.
  if(target>=original_aspect)
    return MainViewAspectScale{original_aspect/target,1.0};
  return MainViewAspectScale{1.0,target/original_aspect};
}
inline std::optional<std::array<float,16>> AdaptVerifiedMainViewProjection(
    const std::array<float,16>& original,
    double original_aspect,
    uint32_t output_width,uint32_t output_height,
    const SourceCameraProof& proof) noexcept {
  if(!proof.original_main_perspective_view ||
     !proof.original_horizontal_fov_verified ||
     !proof.original_view_projection_abi_verified ||
     proof.convention==ClipMatrixConvention::Unknown ||
     !output_width || !output_height)
    return std::nullopt;
  const auto scales=ExpandAspectNoCrop(original_aspect,output_width,output_height);
  if(!scales)return std::nullopt;
  for(const auto x:original)
    if(!std::isfinite(x))return std::nullopt;
  auto matrix=original;
  // Never alter guest XDK RT dimensions, EDRAM layout or interpolated UVs.
  // Column-vector: clip = M*p -> scale rows x/y.
  // Row-vector:    clip = p*M -> scale columns x/y.
  for(size_t axis=0;axis<2;++axis) {
    const double factor=axis==0 ? scales->clip_x : scales->clip_y;
    for(size_t i=0;i<4;++i) {
      const size_t at=proof.convention==ClipMatrixConvention::ColumnVector ?
           axis*4+i : i*4+axis;
      const double corrected=double(matrix[at])*factor;
      if(!std::isfinite(corrected) ||
         corrected>double(3.402823466e38f) ||
         corrected<-double(3.402823466e38f))
        return std::nullopt;
      matrix[at]=static_cast<float>(corrected);
    }
  }
  return matrix;
}
// Keep the source-compatible name: previous callers used this for the
// narrower 16:10 only. Now it also correctly handles 21:9, 32:9 and 4:3.
inline std::optional<std::array<float,16>> ExpandVerifiedVerticalView(
    const std::array<float,16>& original,
    double original_aspect,
    uint32_t width,uint32_t height,
    const SourceCameraProof& proof) noexcept {
  return AdaptVerifiedMainViewProjection(original,original_aspect,
                                         width,height,proof);
}
}  // namespace hulk::camera
