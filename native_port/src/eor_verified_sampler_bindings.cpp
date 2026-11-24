#include "eor_verified_sampler_bindings.h"
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
namespace hulk::eor::gpu {
void ValidateOriginalEorSamplers(std::span<const VerifiedEorSampler> slots) {
  if(slots.size()>128)
    throw std::invalid_argument("Excess original EoR shader samplers");
  std::set<std::pair<int,std::string>> used_names;
  std::set<std::pair<int,uint32_t>> used_fetches;
  uint32_t model=0,surface=UINT32_MAX,effect=0,pass=UINT32_MAX;
  for(const auto& slot:slots) {
    if(!slot.original_model_hash ||
       slot.original_surface_ordinal==UINT32_MAX ||
       !slot.original_effect_hash ||
       slot.original_pass_index==UINT32_MAX ||
       (bool(slot.original_texture_hash)==bool(slot.original_resolved_guest_object)) ||
       (slot.original_resolved_guest_object && slot.stage!=OriginalSamplerStage::Pixel) ||
       slot.original_fetch_register==UINT32_MAX ||
       slot.original_fetch_register>=128 ||
       (slot.stage!=OriginalSamplerStage::Vertex &&
        slot.stage!=OriginalSamplerStage::Pixel) ||
       slot.exact_shader_variable_name.empty() ||
       slot.exact_shader_variable_name.size()>128 ||
       !slot.original_material_ownership_verified ||
       !slot.original_shader_fetch_slot_verified ||
       !slot.original_texture_format_and_mips_verified)
      throw std::invalid_argument("EoR original shader/material/sampler identity unproven");
    for(unsigned char c:slot.exact_shader_variable_name)
      if(c<0x21 || c>0x7e || c=='/' || c=='\\')
        throw std::invalid_argument("Unsafe original EoR shader variable name");
    if(!model) {
      model=slot.original_model_hash;
      surface=slot.original_surface_ordinal;
      effect=slot.original_effect_hash;
      pass=slot.original_pass_index;
    } else if(model!=slot.original_model_hash ||
              surface!=slot.original_surface_ordinal ||
              effect!=slot.original_effect_hash ||
              pass!=slot.original_pass_index) {
      throw std::invalid_argument("Mixed original EoR Draw sampler ownership");
    }
    const int stage=static_cast<int>(slot.stage);
    if(!used_names.emplace(stage,slot.exact_shader_variable_name).second ||
       !used_fetches.emplace(stage,slot.original_fetch_register).second)
      throw std::invalid_argument("Ambiguous original EoR fetch/host shader sampler slot");
  }
}
} // namespace hulk::eor::gpu
