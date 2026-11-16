#pragma once
// Owned host-native color/depth scene images and MSAA resolve for EoR frames.
// No Xenos EDRAM aliasing; no guessed game render-state, sampler or PSO.
// Must run on the render thread of an existing DiligentCore/Vulkan device.
#include "eor_native_frame.h"
#include <cstdint>
#include <memory>
namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
}
namespace hulk::eor::gpu {
// Explicit original EoR game color/depth format evidence must be converted
// to these host formats by the caller; never select one from surface size.
enum class NativeColorFormat : uint8_t { Unknown, Rgba8, Rgba16Float };
enum class NativeDepthFormat : uint8_t { Unknown, D24S8, D32Float };
struct VerifiedSceneFormat {
  NativeColorFormat color = NativeColorFormat::Unknown;
  NativeDepthFormat depth = NativeDepthFormat::Unknown;
  bool original_color_format_verified = false;
  bool original_depth_format_verified = false;
};
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
  bool BeginScene(); // does not clear: the real EoR Clear/Depth ABI owns it
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
