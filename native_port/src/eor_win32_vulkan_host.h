#pragma once
// Win32 native EoR Vulkan device and swapchain owner. This is intentionally
// independent of ReXGlue, Xenia, Xenos PM4, EDRAM and NativeX Device.
// The original game code and verified EoR draw backend can share its
// DiligentCore handles; never create a second swapchain on the same HWND.
#include <cstdint>
#include <functional>
#include <memory>
#include "eor_native_frame.h"

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ISwapChain;
struct ITextureView;
}
namespace hulk::eor::gpu {
class EorWin32VulkanHost final {
 public:
  EorWin32VulkanHost() noexcept;
  ~EorWin32VulkanHost() noexcept;
  EorWin32VulkanHost(const EorWin32VulkanHost&)=delete;
  EorWin32VulkanHost& operator=(const EorWin32VulkanHost&)=delete;

  // Source is an actual Win32 game HWND. The native host is its exclusive
  // Vulkan swapchain owner; its color is the native surface format (no guest gamma pass).
  bool Create(void* hwnd) noexcept;
  bool ResizeToClient() noexcept;
  bool Present(bool vsync) noexcept;
  // A verified EoR backend must hand off its actual fully resolved,
  // physical-sized color target. Copy exact Vulkan pixels (or accept an
  // already-rendered native backbuffer), then ONE physical Present.
  // No Xenos/EDRAM, NativeX blitter, guest upscaling or second swapchain.
  bool PresentOriginalColor(Diligent::ITextureView* source,bool vsync) noexcept;
  bool CompositeOriginalColor(Diligent::ITextureView* source,native::Rect rect,
                              native::Size physical,bool vsync) noexcept;
  void Shutdown() noexcept;

  Diligent::IRenderDevice* Device() const noexcept;
  Diligent::IDeviceContext* Context() const noexcept;
  Diligent::ISwapChain* SwapChain() const noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
// The same signature accepted by DiligentEorDirectBackend::CompositeOutput.
// Source-validated EoR Draw output reaches the native Win32 host without a
// NativeX blitter, SDK guest frame or interim 16:9 render texture.
// The callback borrows host; the owner must keep the host alive.
using NativeWin32EorCompositeOutput=std::function<bool(
    Diligent::ITextureView*,native::Rect,native::Size)>;
NativeWin32EorCompositeOutput BindNativeWin32EorVulkanOutput(
    EorWin32VulkanHost& host,bool vsync=false);
} // namespace hulk::eor::gpu
