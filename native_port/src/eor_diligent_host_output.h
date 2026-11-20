#pragma once
// Actual independent EoR host Vulkan image -> game window path.
// Reuses the already linked NativeXRenderer DiligentCore Device and Blitter;
// does NOT run NativeXRenderer's Xenos Draw/PM4/EDRAM pipeline.
//
// IMPORTANT: this object does not take ownership of the SDK's window or
// decide when the source Xbox game is ready for native presentation.
// The caller must first validate the entire original frame, the model /
// shader / render-target provenance and exclusive window-swapchain handoff.
#include "eor_native_frame.h"
#include <cstdint>
#include <functional>
#include <memory>
namespace Diligent {struct ITextureView;}
namespace native_x_renderer {class Device;class Blitter;}
namespace hulk::eor::gpu {
class DiligentEorHostOutput final {
 public:
  explicit DiligentEorHostOutput(native_x_renderer::Device& host);
  DiligentEorHostOutput(const DiligentEorHostOutput&)=delete;
  DiligentEorHostOutput& operator=(const DiligentEorHostOutput&)=delete;
  ~DiligentEorHostOutput() noexcept;
  // Actual Diligent Vulkan shaders for blitting are created here, BEFORE
  // first presentation, not during the source game's Draw.
  bool Initialize();
  // Match the DiligentEorDirectBackend::CompositeOutput signature. Caller
  // gives the source-verified scene SRV (resolved, host physical dimensions)
  // and exactly the selected real host destination. The host Vulkan device
  // must exclusively own the target window (no simultaneous Xenos/NativeX).
  // NativeX worker mode: write the source-verified physical-sized frame
  // into its CURRENT backbuffer, detach the RTV, and DO NOT Present.
  // The worker runs overlays and FinishPresent exactly once afterwards.
  bool CompositeToBackbuffer(Diligent::ITextureView* resolved_scene,
                             native::Rect destination,native::Size backbuffer);
  // Standalone owner mode: copy + one Present. Never use this entry inside
  // NativeX's renderer worker (that would double-submit a frame).
  bool CompositeAndPresent(Diligent::ITextureView* resolved_scene,
                           native::Rect destination,native::Size backbuffer);
  uint64_t PresentedFrames() const noexcept {return presented_;}
 private:
  native_x_renderer::Device* device_=nullptr;
  std::unique_ptr<native_x_renderer::Blitter> blitter_;
  bool initialized_=false;
  uint64_t presented_=0;
};
// An actual DiligentEorDirectBackend::CompositeOutput-compatible function.
// Pass to the existing native EoR renderer constructor once the caller has
// finished source provenance and exclusive swapchain ownership checks.
using NativeEorCompositeOutput=std::function<bool(
    Diligent::ITextureView*,native::Rect,native::Size)>;
NativeEorCompositeOutput BindNativeEorVulkanBackbuffer(
    DiligentEorHostOutput& host);
NativeEorCompositeOutput BindNativeEorVulkanPresentation(
    DiligentEorHostOutput& host);
} // namespace hulk::eor::gpu
