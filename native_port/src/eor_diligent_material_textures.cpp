#include "eor_diligent_material_textures.h"
#include "ShaderResourceBinding.h"
#include "ShaderResourceVariable.h"
#include "TextureView.h"
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
