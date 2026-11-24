#pragma once
// Owned host-native color/depth scene images and MSAA resolve for EoR frames.
// No Xenos EDRAM aliasing; no guessed game render-state, sampler or PSO.
// Must run on the render thread of an existing DiligentCore/Vulkan device.
#include "eor_native_frame.h"
#include "eor_xdk_draw_capture.h"
#include <cstdint>
#include <memory>
#include <optional>
namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
}
namespace hulk::eor::gpu {
// Explicit original EoR game color/depth format evidence must be converted
// to these host formats by the caller; never select one from surface size.
enum class NativeColorFormat : uint8_t { Unknown, Rgba8, Rgba16Float };
enum class NativeDepthFormat : uint8_t { Unknown, D24S8, D32Float, None };
// Original XDK device/screen identities must come from an independent
// USA executable memory proof. Renderer must NEVER derive them from a
// shader name, material hash or nearest active PM4 target.
struct VerifiedOriginalXdkScreen {
  uint32_t guest_device=0;
  uint32_t guest_rt0=0;
  uint32_t guest_depth=0;
  uint32_t guest_front_buffer=0;
  bool exact_guest_to_native_targets_verified=false;
  bool final_resolve_to_front_buffer_verified=false;
  bool original_tiling_has_no_host_aliases_verified=false;
};

 // A live USA Draw must still reference the SAME original RT0 and depth
 // surface that this native Vulkan render pass owns. Matching only model,
 // material and shader digests cannot detect guest target switches that
 // bypass the observed SetRenderTarget chronology.
inline bool MatchesVerifiedOriginalXdkMainDrawTargets(
    const xdk::OriginalVsMatrices& observed,
    const VerifiedOriginalXdkScreen& screen,bool depth_bound=true) noexcept {
  return screen.exact_guest_to_native_targets_verified &&
         screen.guest_device && screen.guest_rt0 && (!depth_bound || screen.guest_depth) &&
         observed.original_guest_device == screen.guest_device &&
         observed.original_rt0_guest_object == screen.guest_rt0 &&
         observed.original_depth_guest_object == (depth_bound ? screen.guest_depth : 0u) &&
         observed.original_vertex_declaration_guest_object &&
         observed.original_vs_guest_object &&
         observed.original_ps_guest_object;
}
struct VerifiedSceneFormat {
  NativeColorFormat color = NativeColorFormat::Unknown;
  NativeDepthFormat depth = NativeDepthFormat::Unknown;
  bool original_color_format_verified = false;
  bool original_depth_format_verified = false;
  VerifiedOriginalXdkScreen original_screen;
};
// Read genuine RB_COLOR0_INFO (0x2001), RB_DEPTH_INFO (0x2002) and
// RB_SURFACE_INFO (0x2000) from the SAME original game XDK Draw.
// This returns the actual GPU surface *formats* for the common supported
// RGBA8, RGBA16F and D24S8 cases. The guest frontbuffer/resolve and
// tiling ownership remain independent, mandatory XDK witnesses: never
// promote this format decoder alone to a permitted Vulkan Present.
struct OriginalSourceTargetFormat {
  VerifiedSceneFormat format;
  uint32_t samples=0;
};
inline std::optional<OriginalSourceTargetFormat>
ResolveOriginalXdkSurfaceFormats(
    const xdk::OriginalVsMatrices& guest,
    const xdk::OriginalXdkFixedGraphicsState& registers) noexcept {
  if(!guest.original_guest_device || !guest.original_rt0_guest_object ||
     registers.original_guest_device!=guest.original_guest_device)
    return std::nullopt;
  const auto surface=registers.ReadOriginalXenosRegister(0x2000u);
  const auto color=registers.ReadOriginalXenosRegister(0x2001u);
  const auto depth=registers.ReadOriginalXenosRegister(0x2002u);
  if(!surface || !color || !depth)return std::nullopt;
  const uint32_t msaa=(*surface>>16u)&3u;
  const uint32_t color_code=(*color>>16u)&15u;
  const uint32_t depth_code=(*depth>>16u)&1u;
  if(msaa>2u)return std::nullopt;
  OriginalSourceTargetFormat result{};
  // Xbox original color format 1 is GAMMA, not RGBA8 linear.
  // 7 is true FP16x4; 0 is UNORM8x4. Other original Xenos formats
  // (10:10:10:2, signed FP10, 16:16 etc.) require explicit conversion.
  if(color_code==0u)result.format.color=NativeColorFormat::Rgba8;
  else if(color_code==7u)result.format.color=NativeColorFormat::Rgba16Float;
  else return std::nullopt;
  if(guest.original_depth_guest_object) {
    // Xenos depth format 1 is 20e4 float-depth+8 stencil, not
    // interchangeable with a host 32f depth format or UNORM24.
    if(depth_code!=0u)return std::nullopt;
    result.format.depth=NativeDepthFormat::D24S8;
  } else {
    result.format.depth=NativeDepthFormat::None;
  }
  result.format.original_color_format_verified=true;
  result.format.original_depth_format_verified=true;
  result.format.original_screen.guest_device=guest.original_guest_device;
  result.format.original_screen.guest_rt0=guest.original_rt0_guest_object;
  result.format.original_screen.guest_depth=guest.original_depth_guest_object;
  result.samples=1u<<msaa;
  // Deliberately do not set exact_guest_to_native_targets_verified,
  // final_resolve_to_front_buffer_verified or
  // original_tiling_has_no_host_aliases_verified.
  return result;
}
class DiligentSceneTargets {
 public:
  DiligentSceneTargets(Diligent::IRenderDevice* device,
                       Diligent::IDeviceContext* context);
  DiligentSceneTargets(const DiligentSceneTargets&) = delete;
  DiligentSceneTargets& operator=(const DiligentSceneTargets&) = delete;
  ~DiligentSceneTargets() noexcept;
  // Allocate color, matching depth and exact-size resolved color from the
  // SAME host frame geometry. Never substitutes a guest 1280x720 surface.
  bool Prepare(const native::FrameGeometry& frame,
               VerifiedSceneFormat verified);
  bool SetDepthBound(bool bind);
  bool DepthBound() const noexcept;
  bool BeginScene(); // does not clear: the real EoR Clear/Depth ABI owns it
  // Execute original XDK ClearF on independently OWNED native RT0/depth.
  // The caller must already prove these host views own the original guest
  // surfaces, and partial guest RECTs must never become full host clears.
  bool ClearVerifiedOriginalXdk(
      const xdk::OriginalXdkClearF& source,native::Size original_guest);
  // Always call after scene draws (even 1x, to unbind the color target).
  // Only an MSAA scene performs a real same-extent Vulkan resolve.
  bool ResolveScene();
  Diligent::ITextureView* ResolvedColorSRV() const noexcept;
  void AbortScene() noexcept;
  void Reset() noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::gpu
