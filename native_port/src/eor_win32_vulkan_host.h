#pragma once
// Win32 native EoR Vulkan device and swapchain owner. This is intentionally
// independent of ReXGlue, Xenia, Xenos PM4, EDRAM and NativeX Device.
// The original game code and verified EoR draw backend can share its
// DiligentCore handles; never create a second swapchain on the same HWND.
#include <cstdint>
#include <memory>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ISwapChain;
}
namespace hulk::eor::gpu {
class EorWin32VulkanHost final {
 public:
  EorWin32VulkanHost() noexcept;
  ~EorWin32VulkanHost() noexcept;
  EorWin32VulkanHost(const EorWin32VulkanHost&)=delete;
  EorWin32VulkanHost& operator=(const EorWin32VulkanHost&)=delete;

  // Source is an actual Win32 game HWND. The native host is its exclusive
  // Vulkan swapchain owner; its color is RGBA8 UNORM (no guest gamma pass).
  bool Create(void* hwnd) noexcept;
  bool ResizeToClient() noexcept;
  bool Present(bool vsync) noexcept;
  void Shutdown() noexcept;

  Diligent::IRenderDevice* Device() const noexcept;
  Diligent::IDeviceContext* Context() const noexcept;
  Diligent::ISwapChain* SwapChain() const noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::gpu
