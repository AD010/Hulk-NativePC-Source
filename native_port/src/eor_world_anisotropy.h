#pragma once
#include <algorithm>
#include <cstdint>

namespace hulk::eor::gpu {
// NativeX and ReXGlue use the Xenos AnisoFilter encoding:
// 0=disabled, 1=1x, 2=2x, 3=4x, 4=8x, 5=16x, -1=original.
// Only proven world-space regular sampled images are eligible.
inline uint32_t NativeWorldAnisotropy(int32_t requested,uint32_t original,
    uint32_t device_max,bool verified_world,bool regular_2d,bool linear_min_mag,
    bool mip_filter_enabled,bool multiple_mips,bool base_resident,
    bool source_adjustments,bool force_point) noexcept {
  if(!verified_world || !regular_2d || !linear_min_mag ||
     !mip_filter_enabled || !multiple_mips || !base_resident ||
     source_adjustments || force_point || requested<0 || requested>5)
    return original;
  const auto desired=requested>1 ? (1u<<uint32_t(requested-1)):1u;
  // Never request a feature the Vulkan adapter cannot support.
  return std::min(desired,std::max(1u,device_max));
}
} // namespace hulk::eor::gpu
