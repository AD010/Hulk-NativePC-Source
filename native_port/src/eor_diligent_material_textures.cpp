#include "eor_diligent_material_textures.h"
#include "ShaderResourceBinding.h"
#include "ShaderResourceVariable.h"
#include "TextureView.h"
#include "RenderDevice.h"
#include "Sampler.h"
#include <stdexcept>
#include <algorithm>
#include <bit>
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
  if(atlas.original_mip_levels!=1 || !atlas.host_texture ||
     atlas.host_texture->GetDesc().Format!=Diligent::TEX_FORMAT_BGRA8_UNORM ||
     (fetch[4]>>2&15u) || (fetch[5]>>12))
    throw std::invalid_argument("Original single-level font atlas unavailable");
  BindOriginalPixelImage2D(device,bindings,atlas,0,fetch,false);
}
void BindOriginalPixelImage2D(Diligent::IRenderDevice* device,
    Diligent::IShaderResourceBinding* bindings,const UploadedEorTexture& texture,
    uint32_t slot,const std::array<uint32_t,6>& fetch,bool force_point,bool xbox_gamma_lowered) {
  using namespace Diligent;
  if(!device || !bindings || slot>=16 || !texture.host_texture || !texture.SRV() ||
     (texture.view_kind!=OriginalTextureViewKind::Native2D &&
      texture.view_kind!=OriginalTextureViewKind::Native1D &&
      texture.view_kind!=OriginalTextureViewKind::NativeCube))
    throw std::invalid_argument("Missing original scalar pixel shader image");
  const bool one_dimensional=texture.view_kind==OriginalTextureViewKind::Native1D;
  const bool cube=texture.view_kind==OriginalTextureViewKind::NativeCube;
  const auto& image=texture.host_texture->GetDesc();
  const auto shape=fetch[2], sampling=fetch[3], format=fetch[1]&63u;
  // These four layouts are decoded by the USA LFXT asset loader. Validate
  // the live swizzle, exponent, signs and typed Vulkan SRV independently.
  const bool compatible=
      (format==6 && image.Format==TEX_FORMAT_BGRA8_UNORM && (sampling&0x7ffffu)==0xc14u) ||
      (format==18 && image.Format==TEX_FORMAT_BC1_UNORM && (sampling&0x7ffffu)==0xd10u) ||
      (format==20 && image.Format==TEX_FORMAT_BC3_UNORM && (sampling&0x7ffffu)==0xd10u) ||
      (format==29 && image.Format==TEX_FORMAT_RGBA16_FLOAT && (sampling&0x7ffffu)==0xd11u);
  const bool compatible_signs=(fetch[0]&1023u)==(xbox_gamma_lowered?254u:2u);
  if(!compatible || !compatible_signs || (xbox_gamma_lowered && !cube) ||
     (fetch[1]&(1u<<10)) ||
     (one_dimensional ?
        ((fetch[5]>>9&3u)!=0u || (shape>>24) ||
         (shape&0xffffffu)+1u!=image.Width || image.Height!=1u) :
        ((fetch[5]>>9&3u)!=(cube?3u:1u) ||
         (shape>>26)!=(cube?5u:0u) ||
         (shape&8191u)+1u!=image.Width || ((shape>>13)&8191u)+1u!=image.Height)) ||
     (cube && (image.ArraySize!=6u || image.Width!=image.Height)) ||
     texture.original_mip_levels!=image.MipLevels || !image.MipLevels || (sampling>>28))
    throw std::invalid_argument("Original pixel fetch constant and resident image disagree");
  auto address=[](uint32_t v) {
    switch(v) {
      case 0:return TEXTURE_ADDRESS_WRAP;
      case 1:return TEXTURE_ADDRESS_MIRROR;
      case 2:return TEXTURE_ADDRESS_CLAMP;
      default:throw std::invalid_argument("Unsupported original texture address mode");
    }
  };
  auto filter=[](uint32_t v) {
    if(v>1)throw std::invalid_argument("Unsupported original texture filter");
    return v ? FILTER_TYPE_LINEAR:FILTER_TYPE_POINT;
  };
  SamplerDesc desc{};
  desc.AddressU=address(fetch[0]>>10&7u);
  desc.AddressV=one_dimensional ? TEXTURE_ADDRESS_CLAMP : address(fetch[0]>>13&7u);
  desc.AddressW=cube ? address(fetch[0]>>16&7u) : TEXTURE_ADDRESS_CLAMP;
  desc.MagFilter=force_point ? FILTER_TYPE_POINT : filter(sampling>>19&3u);
  desc.MinFilter=force_point ? FILTER_TYPE_POINT : filter(sampling>>21&3u);
  const auto mip=force_point ? 0u : (sampling>>23&3u);
  desc.MipFilter=mip==2u ? FILTER_TYPE_POINT : filter(mip);
  const auto last=std::bit_width(std::max(image.Width,image.Height))-1u;
  uint32_t lo=0,hi=0;
  if(fetch[5]>>12&0x1ffffu) {
    lo=std::min(fetch[4]>>2&15u,last);
    hi=std::max(lo,std::min(fetch[4]>>6&15u,last));
  }
  if(!(fetch[1]>>12&0x1ffffu)) {
    if(!hi)throw std::invalid_argument("Original image has no resident base or mip page");
    lo=std::max(lo,1u);
  }
  if(mip==2u)hi=lo;
  if(hi>=image.MipLevels)
    throw std::invalid_argument("Source sampler requires a missing original mip level");
  desc.MinLOD=static_cast<float>(lo);
  desc.MaxLOD=static_cast<float>(hi);
  const auto bias_bits=static_cast<int>((fetch[4]>>12)&1023u);
  desc.MipLODBias=static_cast<float>(bias_bits>=512 ? bias_bits-1024 : bias_bits)/32.0f;
  if((fetch[4]>>22)!=0 && (hi!=lo || desc.MinFilter!=desc.MagFilter))
    throw std::invalid_argument("Original gradient adjustment affects texture filtering");
  const auto aniso=sampling>>25&7u;
  if(aniso>1u) {
    if(aniso>5u || desc.MinFilter!=FILTER_TYPE_LINEAR || desc.MagFilter!=FILTER_TYPE_LINEAR ||
       desc.MipFilter!=FILTER_TYPE_LINEAR || (fetch[5]>>5&15u))
      throw std::invalid_argument("Original anisotropic filter combination unsupported");
    desc.MaxAnisotropy=1u<<(aniso-1u);
    if(desc.MaxAnisotropy>device->GetAdapterInfo().Sampler.MaxAnisotropy)
      throw std::invalid_argument("Native device cannot reproduce original anisotropy");
    desc.MinFilter=desc.MagFilter=desc.MipFilter=FILTER_TYPE_ANISOTROPIC;
  }
  const auto suffix=std::to_string(slot);
  auto* resource=bindings->GetVariableByName(SHADER_TYPE_PIXEL,((cube?"HulkTextureCube_s":one_dimensional?"HulkTexture1D_s":"HulkTexture2D_s")+suffix).c_str());
  auto* sampler=bindings->GetVariableByName(SHADER_TYPE_PIXEL,("HulkSampler_s"+suffix).c_str());
  if(!resource || !sampler)
    throw std::invalid_argument("Original pixel image or sampler descriptor missing");
  RefCntAutoPtr<ISampler> native;
  device->CreateSampler(desc,&native);
  if(!native)throw std::runtime_error("Original texture sampler creation failed");
  resource->Set(texture.SRV());
  sampler->Set(native);
}

void PrepareOriginalResolvedPixelSampler(Diligent::IRenderDevice* device,
    Diligent::IShaderResourceBinding* bindings,uint32_t slot,
    const std::array<uint32_t,6>& fetch,bool force_point,
    uint32_t width,uint32_t height,bool fp16) {
  using namespace Diligent;
  if(!device || !bindings || slot>=16 || !width || !height ||
     (fetch[0]&1023u)!=2u || (fetch[1]&(1u<<10)) ||
     (fetch[1]&63u)!=(fp16?29u:6u) ||
     (fetch[5]>>9&3u)!=1u || (fetch[2]>>26) ||
     (fetch[2]&8191u)+1u!=width || ((fetch[2]>>13)&8191u)+1u!=height ||
     (fetch[3]&0x7ffffu)!=(fp16?0xd11u:0xd10u) ||
     (fetch[4]>>2&255u) || (fetch[3]>>28) ||
     !(fetch[1]>>12&0xfffffu))
    throw std::invalid_argument("Original full Resolve texture fetch contract differs from native image");
  auto address=[](uint32_t value) {
    switch(value) {
      case 0:return TEXTURE_ADDRESS_WRAP;
      case 1:return TEXTURE_ADDRESS_MIRROR;
      case 2:return TEXTURE_ADDRESS_CLAMP;
      default:throw std::invalid_argument("Unsupported original Resolve address mode");
    }
  };
  auto filter=[](uint32_t value) {
    if(value>1u)throw std::invalid_argument("Unsupported original Resolve filter");
    return value?FILTER_TYPE_LINEAR:FILTER_TYPE_POINT;
  };
  if((fetch[3]>>25&7u)>1u)throw std::invalid_argument("Original Resolve anisotropy unsupported");
  SamplerDesc desc{};
  desc.AddressU=address(fetch[0]>>10&7u);
  desc.AddressV=address(fetch[0]>>13&7u);
  desc.AddressW=TEXTURE_ADDRESS_CLAMP;
  desc.MinFilter=force_point?FILTER_TYPE_POINT:filter(fetch[3]>>21&3u);
  desc.MagFilter=force_point?FILTER_TYPE_POINT:filter(fetch[3]>>19&3u);
  desc.MipFilter=FILTER_TYPE_POINT;
  desc.MinLOD=desc.MaxLOD=0;
  const auto suffix=std::to_string(slot);
  auto* image=bindings->GetVariableByName(SHADER_TYPE_PIXEL,("HulkTexture2D_s"+suffix).c_str());
  auto* sampler=bindings->GetVariableByName(SHADER_TYPE_PIXEL,("HulkSampler_s"+suffix).c_str());
  if(!image || !sampler)throw std::invalid_argument("Missing native Resolve SRB variables");
  RefCntAutoPtr<ISampler> native;
  device->CreateSampler(desc,&native);
  if(!native)throw std::runtime_error("Original Resolve sampler allocation failed");
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
    if(!sampler.identity.original_resolved_guest_object &&
       (!sampler.uploaded ||
       sampler.uploaded->original_texture_hash!=sampler.identity.original_texture_hash ||
       sampler.uploaded->view_kind!=sampler.required_view ||
       !sampler.uploaded->original_mip_levels || !sampler.uploaded->SRV()))
      throw std::invalid_argument("Original EoR sampler has no matching resident GPU texture");
    auto* var=srb->GetVariableByName(
       Stage(sampler.identity.stage),
       sampler.identity.exact_shader_variable_name.c_str());
    if(!var) throw std::invalid_argument(
        "Original shader resource variable missing from the exact current Vulkan PSO");
    variables.push_back(var);
  }
  for(size_t i=0;i<samplers.size();++i)
    if(!samplers[i].identity.original_resolved_guest_object)
      variables[i]->Set(samplers[i].uploaded->SRV());
}
} // namespace hulk::eor::gpu
