#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <optional>

namespace hulk::eor::gpu {
struct OriginalMipWindow {
  uint32_t base=0,count=0;
};
// Find the exact guest-visible base in an independently verified complete
// original archive mip chain. Vulkan's SRV makes relative LOD zero refer
// to that level; never silently substitute the largest texture level.
inline std::optional<OriginalMipWindow> PlanOriginalMipWindow(
    uint32_t width,uint32_t height,uint32_t levels,
    uint32_t guest_width,uint32_t guest_height) noexcept {
  if(!width || !height || !guest_width || !guest_height ||
     !levels || levels>std::bit_width(std::max(width,height)))return {};
  for(uint32_t i=0;i<levels;++i)
    if(std::max(1u,width>>i)==guest_width &&
       std::max(1u,height>>i)==guest_height)
      return OriginalMipWindow{i,levels-i};
  return {};
}
// Xenos min/max fields are relative to the bound image's base, not the
// original full archive dimensions. No missing level may be synthesized.
struct OriginalSamplerMipRange {uint32_t lo=0,hi=0;};
inline std::optional<OriginalSamplerMipRange> PlanOriginalSamplerMipRange(
    uint32_t width,uint32_t height,uint32_t available_levels,
    bool base_resident,bool mip_resident,uint32_t lo,uint32_t hi,
    bool mip_disabled) noexcept {
  if(!width || !height || !available_levels || lo>15 || hi>15 ||
     available_levels>std::bit_width(std::max(width,height)))return {};
  const auto last=std::bit_width(std::max(width,height))-1u;
  if(mip_resident) {
    lo=std::min(lo,last);hi=std::max(lo,std::min(hi,last));
  } else lo=hi=0;
  if(!base_resident) {
    if(!hi)return {};
    lo=std::max(lo,1u);
  }
  if(mip_disabled)hi=lo;
  if(hi>=available_levels)return {};
  return OriginalSamplerMipRange{lo,hi};
}
} // namespace hulk::eor::gpu
