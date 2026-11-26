#pragma once
// Execute a complete source-verified original EoR frame on an exclusively
// owned native Win32 Vulkan device/swapchain. No XEX, ReXGlue, NativeX, Xenos
// register interpreter, EDRAM or guest presentation is required by this API.
// The caller supplies authentic frame provenance and source-owned materials.
#include "eor_aot_draw_bridge.h"
#include "eor_win32_vulkan_host.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace hulk::eor::gpu {
class DiligentEorDirectBackend;
enum class NativeFrameSubmission : uint8_t {
  IncompleteSource, Submitted, FailedAfterGpu
};
class EorNativeFrameExecutor final {
 public:
  EorNativeFrameExecutor(
      EorWin32VulkanHost& host,DiligentEorDirectBackend& backend,
      const aot::ShaderBank& shader_bank,const aot::EffectCatalog& effects,
      const aot::ReflectionBank& reflection) noexcept;
  // Transitional game worker: borrow its existing native Vulkan handles.
  // That worker retains exclusive resize/physical Present ownership.
  // Handles and backend must outlive this executor and remain unchanged.
  EorNativeFrameExecutor(
      Diligent::IRenderDevice* device,Diligent::IDeviceContext* context,
      Diligent::ISwapChain* swapchain,DiligentEorDirectBackend& backend,
      const aot::ShaderBank& shader_bank,const aot::EffectCatalog& effects,
      const aot::ReflectionBank& reflection) noexcept;
  ~EorNativeFrameExecutor() noexcept;
  EorNativeFrameExecutor(const EorNativeFrameExecutor&)=delete;
  EorNativeFrameExecutor& operator=(const EorNativeFrameExecutor&)=delete;

  // Original Draw/material/mesh sources must already have been verified
  // against the same immutable EoR XDK frame. An empty, partial or letter-
  // boxed frame is not an independent PC-native render. The optional
  // retained owners keep resident geometry/texture buffers alive until the
  // actual Vulkan GPU fence, even during large city destructions.
  NativeFrameSubmission Submit(
      const native::FrameGeometry& physical_frame,
      std::span<const aot::BoundIndexedDraw> ordered_draws,
      const xdk::Frame& source,
      std::vector<std::shared_ptr<const void>> retained_owners={}) noexcept;

  // Poll on the same native Vulkan render thread as Submit; never retire
  // transient font/debris buffers just because the source frame advanced.
  void RetireCompleted() noexcept;
  void Shutdown() noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::gpu
