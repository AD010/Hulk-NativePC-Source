#include "eor_diligent_host_output.h"
#ifdef _WIN32
#include "eor_vulkan_color_transfer.h"
#endif
#include "blit.h"
#include "device.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "Texture.h"
#include "TextureView.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace hulk::eor::gpu {
DiligentEorHostOutput::DiligentEorHostOutput(native_x_renderer::Device& host)
    :device_(&host){}
DiligentEorHostOutput::DiligentEorHostOutput(
    Diligent::IRenderDevice& device,Diligent::IDeviceContext& context,
    Diligent::ISwapChain& swapchain)
    :native_device_(&device),native_context_(&context),native_swapchain_(&swapchain){}
DiligentEorHostOutput::~DiligentEorHostOutput() noexcept=default;
bool DiligentEorHostOutput::Initialize() {
  if(initialized_)return true;
  if(native_device_ && native_context_ && native_swapchain_ && !device_) {
    // Both Windows EoR output owners use the same Vulkan transfer
    // implementation. A borrowed native device does not imply a dependency
    // on NativeX's fullscreen shader compiler or its blit pipelines.
#ifdef _WIN32
    try {
      auto transfer=std::make_unique<VulkanColorTransfer>();
      if(!transfer->Initialize(native_device_,native_context_))return false;
      native_transfer_=std::move(transfer);
    } catch (...) {return false;}
#endif
    initialized_=true;
    return true;
  }
  if(!device_ || !device_->device() || !device_->context() ||
     !device_->swap_chain())return false;
  try {
    auto instance=std::make_unique<native_x_renderer::Blitter>();
    if(!instance->Initialize(device_->device()))return false;
    blitter_=std::move(instance);
    initialized_=true;
    return true;
  } catch (...) {return false;}
}
bool DiligentEorHostOutput::CompositeToBackbuffer(
    Diligent::ITextureView* source,native::Rect dest,native::Size output) {
  if(!initialized_ || !source || !source->GetTexture() ||
     !output.width || !output.height ||
     output.width>16384 || output.height>16384 ||
     !dest.width || !dest.height ||
     uint64_t(dest.left)+dest.width>output.width ||
     uint64_t(dest.top)+dest.height>output.height ||
     (!device_ && !native_swapchain_))return false;
  // Independent EoR has its own Vulkan device/context/swapchain; the
  // compatibility path still uses NativeX's window-resize ownership.
  if(device_ && !device_->UpdateSize())return false;
  auto* chain=device_?device_->swap_chain():native_swapchain_;
  auto* context=device_?device_->context():native_context_;
  if(!chain || !context)return false;
  const auto& desc=chain->GetDesc();
  if(desc.Width!=output.width || desc.Height!=output.height)
    return false;
  auto* target=chain->GetCurrentBackBufferRTV();
  if(!target)return false;
  const auto& original=source->GetTexture()->GetDesc();
  // A guest 720p image is NOT a native 1440p/1600p EoR scene. The
  // preflight contract requires exact source pixels, a real resolved
  // 1x color SRV and a host-sized destination before we touch the
  // backbuffer. Never fall back to an upscale during Vulkan handoff.
  if(original.SampleCount!=1 ||
     source->GetDesc().ViewType!=Diligent::TEXTURE_VIEW_SHADER_RESOURCE ||
     !native::HasFullNativeCompositePixels(
         {original.Width,original.Height},dest,output))
    return false;
#ifdef _WIN32
  if(native_device_ && !device_) {
    // Never fall back to the compatibility blitter after touching Vulkan.
    // This path shares exact-copy / verified RGBA8<->BGRA8 conversion
    // semantics with the sole independent Win32 swapchain owner.
    try {return native_transfer_ && native_transfer_->Copy(source,target);}
    catch (...) {return false;}
  }
#endif
  // Original EoR already produced the finished physical-resolution image.
  // When its Vulkan format exactly matches the physical swapchain, use a
  // native GPU texture copy instead of NativeX's rasterizing blitter. This
  // bypasses the compatibility renderer for the common unscaled path, and
  // avoids an extra full-screen shader draw. Conversion (HDR/sRGB/channel
  // order) still goes through the existing proven blitter, never a fake
  // same-format copy.
  auto* backbuffer=target->GetTexture();
  // A native producer is allowed to render directly to this backbuffer.
  // Sampling its own active RTV with a fullscreen pass would introduce a
  // Vulkan read/write feedback hazard. In this one case the desired pixels
  // are already present; do not issue another GPU command.
  if(backbuffer && source->GetTexture()==backbuffer &&
     source->GetDesc().Format==target->GetDesc().Format &&
     dest.left==0 && dest.top==0 &&
     dest.width==output.width && dest.height==output.height)
    return true;
  if(backbuffer && source->GetTexture()!=backbuffer &&
     dest.left==0 && dest.top==0 && dest.width==output.width &&
     dest.height==output.height) {
    const auto& back=backbuffer->GetDesc();
    if(original.Width==back.Width && original.Height==back.Height &&
       original.Format==back.Format &&
       source->GetDesc().Format==target->GetDesc().Format &&
       back.SampleCount==1 &&
       original.ArraySize==1 && back.ArraySize==1 &&
       original.MipLevels==1 && back.MipLevels==1) {
      try {
        context->SetRenderTargets(0,nullptr,nullptr,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        Diligent::CopyTextureAttribs copy{};
        copy.pSrcTexture=source->GetTexture();
        copy.pDstTexture=backbuffer;
        copy.SrcTextureTransitionMode=Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        copy.DstTextureTransitionMode=Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);
        return true;
      } catch (...) {
        // The swapchain may be lost/resized; do not draw another composite
        // over a possibly partially submitted direct copy.
        return false;
      }
    }
  }
  // Native EoR can perform the exact GPU copy without constructing any
  // compatibility utility shader. Only verified physical-pixel
  // HDR/sRGB/format conversions still need the small blit pipeline.
  if(!blitter_) {
    try {
      auto instance=std::make_unique<native_x_renderer::Blitter>();
      auto* render_device=device_?device_->device():native_device_;
      if(!render_device || !instance->Initialize(render_device))return false;
      blitter_=std::move(instance);
    } catch (...) {return false;}
  }
  // Keep the swapchain RTV detached on every exit, including exceptions
  // raised by the blitter or Present during a resize/device-loss transition.
  bool rtv_bound=false;
  const auto detach=[&]() noexcept -> bool {
    if(!rtv_bound)return true;
    try {
      context->SetRenderTargets(0,nullptr,nullptr,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
    } catch (...) {
      // Device loss may prevent detachment; do not issue Present afterwards.
      return false;
    }
    rtv_bound=false;
    return true;
  };
  try {
    // Only a full native source may reach the host. HUD/camera provenance
    // must be verified earlier, not hidden behind black-bar margins.
    auto* rtv=target;
    // Mark the attempted binding before the driver call: a backend may
    // change render-target state and then throw during a device-loss/resize.
    // The exception path must still attempt to detach that backbuffer.
    rtv_bound=true;
    context->SetRenderTargets(
        1,&rtv,nullptr,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    // The validated source covers every physical backbuffer pixel.
    // Do not clear the swapchain before the exact-size copy: a redundant
    // full-screen GPU write wastes bandwidth on every destruction-heavy
    // frame and can introduce a black flash if the copy fails.
    const native_x_renderer::Blitter::Rect from{
        0,0,original.Width,original.Height};
    const native_x_renderer::Blitter::Rect to{
        static_cast<int32_t>(dest.left),
        static_cast<int32_t>(dest.top),dest.width,dest.height};
    // Exact physical pixel copy only. kColorScaled is prohibited: it
    // masks source/guest RT mismatches as blurry high-resolution output.
    blitter_->Copy(context,source,
       native_x_renderer::Blitter::Source::kColor,
       from,rtv,to);
    // Release the swapchain RTV before Present/resize. Otherwise the
    // context can retain a reference to the outgoing backbuffer across a
    // window resize, even though the source EoR scene has already resolved.
    if(!detach())return false;
    // NativeX's sole worker can now finish this frame (overlay + exactly
    // ONE physical Present) without a second swap or a 720p guest copy.
    return true;
  } catch (...) {
    detach();
    return false;
  }
}
bool DiligentEorHostOutput::CompositeAndPresent(
    Diligent::ITextureView* source,native::Rect dest,native::Size output) {
  if(!CompositeToBackbuffer(source,dest,output))return false;
  try {
    // Standalone/direct host ownership only. Do not use this entry when
    // invoked by NativeX's existing worker, which calls FinishPresent().
    if(native_swapchain_)native_swapchain_->Present(native_vsync_?1u:0u);
    else device_->Present();
    ++presented_;
    return true;
  } catch (...) {return false;}
}
NativeEorCompositeOutput BindNativeEorVulkanBackbuffer(
    DiligentEorHostOutput& host) {
  // A source-proven EoR backend may use NativeX's already-owned worker;
  // this callback writes physical pixels but NEVER calls Present itself.
  return [&host](Diligent::ITextureView* color,
                 native::Rect target,native::Size host_size) {
    return host.CompositeToBackbuffer(color,target,host_size);
  };
}
NativeEorCompositeOutput BindNativeEorVulkanPresentation(
    DiligentEorHostOutput& host) {
  // Calls the physical Vulkan swapchain's Present(), not guest EDRAM
  // Resolve/PM4 or an unrelated SDK Xenos swapchain.
  return [&host](Diligent::ITextureView* color,
                 native::Rect target,native::Size host_size) {
    return host.CompositeAndPresent(color,target,host_size);
  };
}
} // namespace hulk::eor::gpu
