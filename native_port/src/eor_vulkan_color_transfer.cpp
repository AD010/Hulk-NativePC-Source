#include "eor_vulkan_color_transfer.h"
#include "eor_native_present_contract.h"
#include "RenderDeviceVk.h"
#include "TextureVk.h"
#include "TextureView.h"

namespace hulk::eor::gpu {
using namespace Diligent;
bool VulkanColorTransfer::Initialize(IRenderDevice* device,IDeviceContext* context) {
  if(!device || !context)return false;
  RefCntAutoPtr<IRenderDeviceVk> vk_device(device,IID_RenderDeviceVk);
  if(!vk_device || !vkGetInstanceProcAddr)return false;
  RefCntAutoPtr<IDeviceContextVk> vk_context(context,IID_DeviceContextVk);
  if(!vk_context)return false;
  const auto instance=vk_device->GetVkInstance();
  const auto get_device=reinterpret_cast<PFN_vkGetDeviceProcAddr>(
      vkGetInstanceProcAddr(instance,"vkGetDeviceProcAddr"));
  const auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
      vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceFormatProperties"));
  if(!properties || !get_device)return false;
  VkFormatProperties rgba{},bgra{};
  properties(vk_device->GetVkPhysicalDevice(),VK_FORMAT_R8G8B8A8_UNORM,&rgba);
  properties(vk_device->GetVkPhysicalDevice(),VK_FORMAT_B8G8R8A8_UNORM,&bgra);
  blit_=reinterpret_cast<PFN_vkCmdBlitImage>(get_device(vk_device->GetVkDevice(),"vkCmdBlitImage"));
  rgba_=rgba.optimalTilingFeatures;
  bgra_=bgra.optimalTilingFeatures;
  context_=std::move(vk_context);
  return true;
}
bool VulkanColorTransfer::Copy(ITextureView* source,ITextureView* destination) {
  if(!context_ || !source || !destination)return false;
  auto* src=source->GetTexture();
  auto* dst=destination->GetTexture();
  if(!src || !dst || src->GetDesc().Type!=RESOURCE_DIM_TEX_2D ||
     dst->GetDesc().Type!=RESOURCE_DIM_TEX_2D)return false;
  const auto surface=[](ITextureView* view) {
    const auto& t=view->GetTexture()->GetDesc();
    const auto& v=view->GetDesc();
    return NativeColorSurface{t.Width,t.Height,uint32_t(t.Format),uint32_t(v.Format),
        t.SampleCount,t.ArraySize,t.MipLevels,v.ViewType==TEXTURE_VIEW_SHADER_RESOURCE,
        v.ViewType==TEXTURE_VIEW_RENDER_TARGET};
  };
  const auto from=surface(source),to=surface(destination);
  const auto linear8=[](uint32_t format) {
    return format==TEX_FORMAT_RGBA8_UNORM || format==TEX_FORMAT_BGRA8_UNORM;
  };
  const auto path=SelectNativeColorPresentPath(from,to,src==dst,
      linear8(from.format) && linear8(to.format) &&
      from.format==from.view_format && to.format==to.view_format);
  if(path==NativeColorPresentPath::Reject)return false;
  context_->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
  if(path==NativeColorPresentPath::AlreadyBackbuffer)return true;
  if(path==NativeColorPresentPath::ExactCopy) {
    context_->CopyTexture({src,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                           dst,RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    return true;
  }
  const auto features=[this](uint32_t format) {
    return format==TEX_FORMAT_RGBA8_UNORM?rgba_:bgra_;
  };
  if(!blit_ || !(features(from.format)&VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
     !(features(to.format)&VK_FORMAT_FEATURE_BLIT_DST_BIT) ||
     src->GetState()==RESOURCE_STATE_UNKNOWN || dst->GetState()==RESOURCE_STATE_UNKNOWN)
    return false;
  RefCntAutoPtr<ITextureVk> input(src,IID_TextureVk),output(dst,IID_TextureVk);
  if(!input || !output)return false;
  // Keep layout tracking in Diligent. GetVkCommandBuffer flushes the barriers.
  StateTransitionDesc barriers[]{
      {src,RESOURCE_STATE_UNKNOWN,RESOURCE_STATE_COPY_SOURCE,STATE_TRANSITION_FLAG_UPDATE_STATE},
      {dst,RESOURCE_STATE_UNKNOWN,RESOURCE_STATE_COPY_DEST,STATE_TRANSITION_FLAG_UPDATE_STATE}};
  context_->TransitionResourceStates(2,barriers);
  const auto commands=context_->GetVkCommandBuffer();
  if(!commands)return false;
  VkImageBlit region{};
  region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
  region.srcOffsets[1]=region.dstOffsets[1]={int32_t(from.width),int32_t(from.height),1};
  blit_(commands,input->GetVkImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        output->GetVkImage(),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_NEAREST);
  return true;
}
}
