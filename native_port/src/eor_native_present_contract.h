#pragma once
// Genuine physical-pixel EoR Vulkan presentation contract. Both Vulkan
// implementations and host-only regression tests use the same safety gate.
// The policy never accepts a 720p upscale, MSAA surface, cross-format
// blit or an unverified sampling-feedback path as an independent frame.
#include <cstdint>
namespace hulk::eor::gpu {
enum class NativeColorPresentPath : uint8_t {
  Reject, AlreadyBackbuffer, ExactCopy
};
struct NativeColorSurface {
  uint32_t width=0, height=0, format=0, view_format=0;
  uint32_t sample_count=0, array_size=0, mip_levels=0;
  bool shader_resource=false, render_target=false;
};
constexpr NativeColorPresentPath SelectNativeColorPresentPath(
    const NativeColorSurface& src,const NativeColorSurface& dst,
    bool same_texture) noexcept {
  if(!src.width || !src.height || !src.format || !src.view_format ||
     src.width!=dst.width || src.height!=dst.height ||
     src.format!=dst.format || src.view_format!=dst.view_format ||
     src.sample_count!=1 || dst.sample_count!=1 ||
     src.array_size!=1 || dst.array_size!=1 ||
     src.mip_levels!=1 || dst.mip_levels!=1 ||
     !dst.render_target)
    return NativeColorPresentPath::Reject;
  if(same_texture)
    return src.render_target?NativeColorPresentPath::AlreadyBackbuffer:
                             NativeColorPresentPath::Reject;
  return src.shader_resource?NativeColorPresentPath::ExactCopy:
                             NativeColorPresentPath::Reject;
}
} // namespace hulk::eor::gpu
