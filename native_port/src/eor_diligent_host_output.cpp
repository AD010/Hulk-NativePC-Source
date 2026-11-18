#include "eor_diligent_host_output.h"
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
DiligentEorHostOutput::~DiligentEorHostOutput() noexcept=default;
bool DiligentEorHostOutput::Initialize() {
  if(initialized_)return true;
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
bool DiligentEorHostOutput::CompositeAndPresent(
    Diligent::ITextureView* source,native::Rect dest,native::Size output) {
  if(!initialized_ || !source || !source->GetTexture() ||
     !output.width || !output.height ||
     output.width>16384 || output.height>16384 ||
     !dest.width || !dest.height ||
     uint64_t(dest.left)+dest.width>output.width ||
     uint64_t(dest.top)+dest.height>output.height ||
     !device_ || !blitter_)return false;
  // The swapchain is the real output, not a scaled 720p guest buffer.
  // The host can have been minimized or resized since the last XDK Draw.
  if(!device_->UpdateSize())return false;
  auto* chain=device_->swap_chain();
  auto* context=device_->context();
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
  try {
    // Only a full native source may reach the host. HUD/camera provenance
    // must be verified earlier, not hidden behind black-bar margins.
    auto* rtv=target;
    context->SetRenderTargets(
        1,&rtv,nullptr,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float black[4]={0.f,0.f,0.f,1.f};
    context->ClearRenderTarget(
        rtv,black,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
    // The same sole host Vulkan device owns both the source EoR scene and
    // output swapchain. No PM4 copy, Xenos EDRAM or competing VkSurface.
    device_->Present();
    ++presented_;
    return true;
  } catch (...) {return false;}
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
