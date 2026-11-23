#include "eor_diligent_material_textures.h"
#include "ShaderResourceBinding.h"
#include "ShaderResourceVariable.h"
#include "TextureView.h"
#include "RenderDevice.h"
#include "Sampler.h"
#include <stdexcept>
#include <vector>
namespace hulk::eor::gpu {
namespace {
Diligent::SHADER_TYPE Stage(OriginalSamplerStage stage) {
  switch(stage) {
    case OriginalSamplerStage::Vertex:return Diligent::SHADER_TYPE_VERTEX;
    case OriginalSamplerStage::Pixel:return Diligent::SHADER_TYPE_PIXEL;
    default:throw std::invalid_argument("Unsupported original EoR shader texture stage");
  }
}
}
void BindUsaFontMainAtlas(Diligent::IRenderDevice* device,
    Diligent::IShaderResourceBinding* bindings,const UploadedEorTexture& atlas,
    const std::array<uint32_t,6>& fetch) {
  using namespace Diligent;
  if(!device || !bindings || !atlas.host_texture || !atlas.SRV() ||
     atlas.original_mip_levels!=1 || atlas.view_kind!=OriginalTextureViewKind::Native2D)
    throw std::invalid_argument("Original single-level font atlas unavailable");
  const auto& image=atlas.host_texture->GetDesc();
  const auto shape=fetch[2],sampling=fetch[3];
  // The authenticated resident BGRA8 atlas is a 2D, normalized unsigned
  // 8:8:8:8 resource. Other swizzles, signs or nonzero exponent adjustments
  // require different shader/image conversions. Never silently reinterpret.
  if((fetch[0]&1023u)!=2u || (fetch[1]&63u)!=6u || !(fetch[1]>>12) ||
     (fetch[1]&(1u<<10)) || (fetch[5]>>9&3u)!=1u || (shape>>26) ||
     (shape&8191u)+1u!=image.Width || ((shape>>13)&8191u)+1u!=image.Height ||
     image.Format!=TEX_FORMAT_BGRA8_UNORM || image.MipLevels!=1 ||
     (sampling&0x7ffffu)!=0xc14u || (sampling>>28) ||
     (fetch[4]>>2&15u) || (fetch[5]>>12))
    throw std::invalid_argument("Font fetch constant and resident atlas disagree");
  auto address=[](uint32_t v) {
    switch(v) {
      case 0:return TEXTURE_ADDRESS_WRAP;
      case 1:return TEXTURE_ADDRESS_MIRROR;
      case 2:return TEXTURE_ADDRESS_CLAMP;
      default:throw std::invalid_argument("Unsupported original font address mode");
    }
  };
  auto filter=[](uint32_t v) {
    if(v>1)throw std::invalid_argument("Unsupported original font filter");
    return v ? FILTER_TYPE_LINEAR:FILTER_TYPE_POINT;
  };
  SamplerDesc desc{};
  desc.AddressU=address(fetch[0]>>10&7u);
  desc.AddressV=address(fetch[0]>>13&7u);
  desc.AddressW=TEXTURE_ADDRESS_CLAMP; // 2D TFETCH has no W coordinate.
  desc.MagFilter=filter(sampling>>19&3u);
  desc.MinFilter=filter(sampling>>21&3u);
  const auto mip=sampling>>23&3u;
  desc.MipFilter=mip==2u ? FILTER_TYPE_POINT : filter(mip);
  desc.MinLOD=desc.MaxLOD=0; // full original chain has a single resident level.
  const auto bias_bits=static_cast<int>((fetch[4]>>12)&1023u);
  desc.MipLODBias=static_cast<float>(bias_bits>=512 ? bias_bits-1024 : bias_bits)/32.0f;
  if((fetch[4]>>22)!=0 && desc.MinFilter!=desc.MagFilter)
    throw std::invalid_argument("Original gradient adjustment affects font filter selection");
  if((sampling>>25&7u)>1u)
    throw std::invalid_argument("Anisotropic original font sampling is unsupported");
  auto* resource=bindings->GetVariableByName(SHADER_TYPE_PIXEL,"HulkTexture2D_s0");
  auto* sampler=bindings->GetVariableByName(SHADER_TYPE_PIXEL,"HulkSampler_s0");
  if(!resource || !sampler)
    throw std::invalid_argument("Original Font_Main slot zero descriptors missing");
  RefCntAutoPtr<ISampler> native;
  device->CreateSampler(desc,&native);
  if(!native)throw std::runtime_error("Original font sampler creation failed");
  resource->Set(atlas.SRV());
  sampler->Set(native);
}

void BindVerifiedOriginalEorTextures(
    Diligent::IShaderResourceBinding* srb,
    std::span<const OriginalDiligentSampler> samplers) {
  if(!srb) throw std::invalid_argument("Vulkan EoR SRB is missing");
  std::vector<VerifiedEorSampler> identities;
  identities.reserve(samplers.size());
  for(const auto& sampler:samplers) identities.push_back(sampler.identity);
  ValidateOriginalEorSamplers(identities);
  // Validate the COMPLETE batch before calling any Diligent Set().
  std::vector<Diligent::IShaderResourceVariable*> variables;
  variables.reserve(samplers.size());
  for(const auto& sampler:samplers) {
    if(!sampler.uploaded ||
       sampler.uploaded->original_texture_hash!=sampler.identity.original_texture_hash ||
       sampler.uploaded->view_kind!=sampler.required_view ||
       !sampler.uploaded->original_mip_levels || !sampler.uploaded->SRV())
      throw std::invalid_argument("Original EoR sampler has no matching resident GPU texture");
    auto* var=srb->GetVariableByName(
       Stage(sampler.identity.stage),
       sampler.identity.exact_shader_variable_name.c_str());
    if(!var) throw std::invalid_argument(
        "Original shader resource variable missing from the exact current Vulkan PSO");
    variables.push_back(var);
  }
  for(size_t i=0;i<samplers.size();++i)
    variables[i]->Set(samplers[i].uploaded->SRV());
}
} // namespace hulk::eor::gpu
