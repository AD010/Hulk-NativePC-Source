// Offscreen Vulkan integration. Supply the original USA INDEX.IND and TEXTURES.ARC.
#include "eor_diligent_texture_residency.h"
#include "DeviceContext.h"
#include "EngineFactoryVk.h"
#include "RenderDevice.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace Diligent;
namespace eor = hulk::eor;

static void VerifyAtlas(IRenderDevice* device, IDeviceContext* context,
                        const eor::textures::OriginalXboxTextureAssets& assets,
                        eor::gpu::DiligentOriginalTextureResidency& residency, uint32_t hash) {
  const auto original=assets.LoadHash(hash);
  if (!original || !original->verified_gpu_mips || original->original_stream)
    throw std::runtime_error("Expected an original resident UI texture");
  const auto& gpu=*original->verified_gpu_mips;
  const auto source=eor::textures::PrepareExplicitOriginalMips(
      gpu.format,gpu.layouts,original->original_bytes,{});
  const auto uploaded=residency.Acquire(
      hash,eor::gpu::OriginalTextureColorSpace::Linear,
      eor::gpu::OriginalTextureViewKind::Native2D);
  auto desc=uploaded->host_texture->GetDesc();
  if (!uploaded->SRV() || desc.Format!=TEX_FORMAT_BGRA8_UNORM || desc.Width!=512 ||
      desc.Height!=256 || desc.MipLevels!=1 || source.mips.size()!=1)
    throw std::runtime_error("Unexpected native UI view or mip layout");

  desc.Name="Original USA atlas readback";
  desc.Usage=USAGE_STAGING;
  desc.BindFlags=BIND_NONE;
  desc.CPUAccessFlags=CPU_ACCESS_READ;
  RefCntAutoPtr<ITexture> staging;
  device->CreateTexture(desc,nullptr,&staging);
  if (!staging) throw std::runtime_error("Vulkan staging texture unavailable");
  CopyTextureAttribs copy{uploaded->host_texture,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                          staging,RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
  context->CopyTexture(copy);
  context->WaitForIdle();
  MappedTextureSubresource mapped{};
  context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
  if (!mapped.pData) throw std::runtime_error("Vulkan atlas readback failed");
  const auto& mip=source.mips.front();
  bool matches=mip.row_pitch_bytes==2048 && mapped.Stride>=mip.row_pitch_bytes &&
               mip.blocks.size()==size_t(desc.Height)*mip.row_pitch_bytes;
  for (Uint32 y=0;matches && y<desc.Height;++y)
    matches=std::memcmp(static_cast<const std::byte*>(mapped.pData)+y*mapped.Stride,
                        mip.blocks.data()+size_t(y)*mip.row_pitch_bytes,mip.row_pitch_bytes)==0;
  context->UnmapTextureSubresource(staging,0,0);
  if (!matches) throw std::runtime_error("Vulkan readback differs from decoded atlas");
  std::cout<<std::hex<<hash<<std::dec<<" matched "<<mip.blocks.size()<<" bytes\n";
}

int main(int argc,char** argv) {
  try {
    if (argc!=3) throw std::invalid_argument("Expected USA INDEX.IND and TEXTURES.ARC paths");
    const auto assets=eor::textures::OriginalXboxTextureAssets::OpenPinnedUsa(argv[1],argv[2]);
    auto* factory=LoadAndGetEngineFactoryVk();
    if (!factory) throw std::runtime_error("Diligent Vulkan factory unavailable");
    EngineVkCreateInfo ci{};
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    factory->CreateDeviceAndContextsVk(ci,&device,&context);
    if (!device || !context) throw std::runtime_error("Vulkan device unavailable");
    std::cout<<device->GetAdapterInfo().Description<<'\n';
    eor::gpu::DiligentOriginalTextureResidency residency(device,assets);
    VerifyAtlas(device,context,assets,residency,0x995ad5dc);
    VerifyAtlas(device,context,assets,residency,0xdad1dd22);
  } catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}
