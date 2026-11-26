#pragma once
#include "volk.h"
#include "DeviceContextVk.h"
#include "RefCntAutoPtr.hpp"

namespace Diligent { struct IRenderDevice; struct ITextureView; }
namespace hulk::eor::gpu {
// Transfer one completed native image on its owning queue. No runtime shader compiler.
class VulkanColorTransfer {
 public:
  bool Initialize(Diligent::IRenderDevice* device,Diligent::IDeviceContext* context);
  bool Copy(Diligent::ITextureView* source,Diligent::ITextureView* destination);
 private:
  Diligent::RefCntAutoPtr<Diligent::IDeviceContextVk> context_;
  PFN_vkCmdBlitImage blit_=nullptr;
  VkFormatFeatureFlags rgba_=0,bgra_=0;
};
}
