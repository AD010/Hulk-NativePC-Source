#include "eor_win32_vulkan_host.h"
#include "eor_native_present_contract.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "EngineFactoryVk.h"
#include "NativeWindow.h"
#include "RefCntAutoPtr.hpp"
#include "DeviceContext.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "Texture.h"
#include "TextureView.h"
#include <memory>
#include <utility>

namespace hulk::eor::gpu {
namespace {
// A 175% Windows scaling setting must not silently turn requested host
// pixels into virtualized logical coordinates during swapchain creation.
// Restore the worker's DPI context after querying the window so the game
// input/menu thread is unaffected.
struct PhysicalClientPixels {
  DPI_AWARENESS_CONTEXT previous=nullptr;
  PhysicalClientPixels() noexcept {
    previous=SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }
  ~PhysicalClientPixels() noexcept {
    if(previous)SetThreadDpiAwarenessContext(previous);
  }
};
}
struct EorWin32VulkanHost::State {
  HWND hwnd=nullptr;
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
  Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapchain;
};
EorWin32VulkanHost::EorWin32VulkanHost() noexcept=default;
EorWin32VulkanHost::~EorWin32VulkanHost() noexcept {Shutdown();}
bool EorWin32VulkanHost::Create(void* window) noexcept {
  if(state_ || !window)return false;
  HWND hwnd=static_cast<HWND>(window);
  PhysicalClientPixels physical;
  RECT rect{};
  if(!IsWindow(hwnd) || !GetClientRect(hwnd,&rect) ||
     rect.right<=rect.left || rect.bottom<=rect.top ||
     rect.right-rect.left>16384 || rect.bottom-rect.top>16384)
    return false;
  try {
    auto* factory=Diligent::GetEngineFactoryVk();
    if(!factory)return false;
    auto state=std::make_unique<State>();
    state->hwnd=hwnd;
    Diligent::EngineVkCreateInfo ci{};
    // EoR uses real indexed Draw/PSO state. These are native Vulkan
    // facilities; no guest PM4 processor or Xenos EDRAM surface exists.
    ci.Features.IndependentBlend=Diligent::DEVICE_FEATURE_STATE_ENABLED;
    ci.Features.DepthClamp=Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    ci.Features.WireframeFill=Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    ci.Features.TextureCompressionBC=Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    ci.Features.AsyncShaderCompilation=Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    ci.NumAsyncShaderCompilationThreads=4;
    ci.DynamicHeapSize=128u<<20;
    ci.DynamicHeapPageSize=1u<<20;
    Diligent::IDeviceContext* contexts[1]{};
    factory->CreateDeviceAndContextsVk(ci,&state->device,contexts);
    state->context.Attach(contexts[0]);
    if(!state->device || !state->context)return false;
    Diligent::SwapChainDesc desc{};
    desc.Width=static_cast<Diligent::Uint32>(rect.right-rect.left);
    desc.Height=static_cast<Diligent::Uint32>(rect.bottom-rect.top);
    // Prefer the actual BGRA8 Win32/Vulkan surface format, also observed
    // as vkFormat=44 on the owner's RTX 5070 Ti. Exact-copy presentation
    // still compares the real allocated formats and never guesses.
    desc.ColorBufferFormat=Diligent::TEX_FORMAT_BGRA8_UNORM;
    desc.DepthBufferFormat=Diligent::TEX_FORMAT_UNKNOWN;
    desc.BufferCount=3;
    desc.Usage=Diligent::SWAP_CHAIN_USAGE_RENDER_TARGET;
    Diligent::Win32NativeWindow native{hwnd};
    factory->CreateSwapChainVk(
        state->device,state->context,desc,native,&state->swapchain);
    if(!state->swapchain)return false;
    state_=std::move(state);
    return true;
  } catch (...) {
    return false;
  }
}
bool EorWin32VulkanHost::ResizeToClient() noexcept {
  if(!state_ || !IsWindow(state_->hwnd))return false;
  PhysicalClientPixels physical;
  RECT rect{};
  if(!GetClientRect(state_->hwnd,&rect) ||
     rect.right<=rect.left || rect.bottom<=rect.top ||
     rect.right-rect.left>16384 || rect.bottom-rect.top>16384)
    return false;
  try {
    const auto w=static_cast<Diligent::Uint32>(rect.right-rect.left);
    const auto h=static_cast<Diligent::Uint32>(rect.bottom-rect.top);
    const auto& old=state_->swapchain->GetDesc();
    if(w!=old.Width || h!=old.Height) {
      // Vulkan backbuffers are still referenced by work submitted for the
      // previous frame. Retire that work before replacing the swapchain.
      state_->context->SetRenderTargets(0,nullptr,nullptr,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
      state_->context->Flush();
      state_->device->IdleGPU();
      state_->swapchain->Resize(w,h);
    }
    return true;
  } catch (...) {return false;}
}
bool EorWin32VulkanHost::Present(bool vsync) noexcept {
  if(!state_ || !IsWindow(state_->hwnd))return false;
  // Rendering targets the physical buffer captured at BeginScene. A resize
  // between Draw/Resolve and Present invalidates that frame: never resize
  // here and present undefined/stale pixels or start a second frame.
  PhysicalClientPixels physical;
  RECT rect{};
  if(!GetClientRect(state_->hwnd,&rect) ||
     rect.right<=rect.left || rect.bottom<=rect.top)return false;
  try {
    const auto& desc=state_->swapchain->GetDesc();
    if(desc.Width!=static_cast<Diligent::Uint32>(rect.right-rect.left) ||
       desc.Height!=static_cast<Diligent::Uint32>(rect.bottom-rect.top) ||
       !state_->swapchain->GetCurrentBackBufferRTV())return false;
    state_->swapchain->Present(vsync?1u:0u);
    return true;
  } catch (...) {return false;}
}
bool EorWin32VulkanHost::PresentOriginalColor(
    Diligent::ITextureView* source,bool vsync) noexcept {
  if(!state_ || !source || !source->GetTexture() ||
     !IsWindow(state_->hwnd))return false;
  PhysicalClientPixels physical;
  RECT rect{};
  if(!GetClientRect(state_->hwnd,&rect) ||
     rect.right<=rect.left || rect.bottom<=rect.top)return false;
  try {
    const auto& desc=state_->swapchain->GetDesc();
    if(desc.Width!=static_cast<Diligent::Uint32>(rect.right-rect.left) ||
       desc.Height!=static_cast<Diligent::Uint32>(rect.bottom-rect.top))
      return false; // the next frame must handle the resize
    auto* current=state_->swapchain->GetCurrentBackBufferRTV();
    if(!current || !current->GetTexture())return false;
    const auto& src=source->GetTexture()->GetDesc();
    const auto& dst=current->GetTexture()->GetDesc();
    const auto from=NativeColorSurface{
        src.Width,src.Height,static_cast<uint32_t>(src.Format),
        static_cast<uint32_t>(source->GetDesc().Format),
        src.SampleCount,src.ArraySize,src.MipLevels,
        source->GetDesc().ViewType==Diligent::TEXTURE_VIEW_SHADER_RESOURCE,
        source->GetDesc().ViewType==Diligent::TEXTURE_VIEW_RENDER_TARGET};
    const auto to=NativeColorSurface{
        dst.Width,dst.Height,static_cast<uint32_t>(dst.Format),
        static_cast<uint32_t>(current->GetDesc().Format),
        dst.SampleCount,dst.ArraySize,dst.MipLevels,
        false,true};
    const auto path=SelectNativeColorPresentPath(
        from,to,source->GetTexture()==current->GetTexture());
    if(path==NativeColorPresentPath::Reject)return false;
    // A prior native Draw may still have the backbuffer bound as RTV.
    // Detach without discarding its contents; it is already complete.
    state_->context->SetRenderTargets(0,nullptr,nullptr,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
    if(path==NativeColorPresentPath::ExactCopy) {
      Diligent::CopyTextureAttribs copy{};
      copy.pSrcTexture=source->GetTexture();
      copy.pDstTexture=current->GetTexture();
      copy.SrcTextureTransitionMode=
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      copy.DstTextureTransitionMode=
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      state_->context->CopyTexture(copy);
    }
    // This method is the exclusive independent physical presenter;
    // it is never invoked from NativeX's existing FinishPresent worker.
    state_->swapchain->Present(vsync?1u:0u);
    return true;
  } catch (...) {
    // A failed GPU copy is NOT permission to present an older guest frame.
    return false;
  }
}
void EorWin32VulkanHost::Shutdown() noexcept {
  if(!state_)return;
  // The native EoR backend can have indexed Draws, resolved images and
  // dynamically mapped constants still pending on the GPU. Wait before
  // destroying the owning VkDevice/VkSwapchain; do not reuse guest fences.
  try {
    if(state_->context)state_->context->Flush();
    if(state_->device)state_->device->IdleGPU();
  } catch (...) {
    // Driver loss still needs the host resources released.
  }
  state_.reset();
}
Diligent::IRenderDevice* EorWin32VulkanHost::Device() const noexcept {
  return state_?state_->device.RawPtr():nullptr;
}
Diligent::IDeviceContext* EorWin32VulkanHost::Context() const noexcept {
  return state_?state_->context.RawPtr():nullptr;
}
Diligent::ISwapChain* EorWin32VulkanHost::SwapChain() const noexcept {
  return state_?state_->swapchain.RawPtr():nullptr;
}
} // namespace hulk::eor::gpu
#endif
