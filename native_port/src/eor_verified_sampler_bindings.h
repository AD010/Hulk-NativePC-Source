#pragma once
// Proven original Xbox 360 EoR material -> Xenos sampler -> shader variable
// contract. It does not discover resource slots or invent a shader ABI.
#include <cstdint>
#include <span>
#include <string>
namespace hulk::eor::gpu {
enum class OriginalSamplerStage : uint8_t {
  Unknown=0, Vertex=1, Pixel=2
};
struct VerifiedEorSampler {
  uint32_t original_model_hash=0;
  uint32_t original_surface_ordinal=UINT32_MAX;
  uint32_t original_effect_hash=0;
  uint32_t original_pass_index=UINT32_MAX;
  uint32_t original_texture_hash=0;
  uint32_t original_fetch_register=UINT32_MAX;
  OriginalSamplerStage stage=OriginalSamplerStage::Unknown;
  std::string exact_shader_variable_name; // verified shader reflection name
  bool original_material_ownership_verified=false;
  bool original_shader_fetch_slot_verified=false;
  bool original_texture_format_and_mips_verified=false;
  // A source Resolve produces a new runtime image, not a MODELS.ARC or
  // TEXTURES.ARC resource hash. Exactly one of these two identities is set.
  uint32_t original_resolved_guest_object=0;
};
// Rejects incomplete/ambiguous original texture ownership before touching GPU.
// These explicit evidentiary flags must originate in a real EoR runtime/Shader
// reflection capture. They are NEVER inferred from nearby model hashes.
void ValidateOriginalEorSamplers(std::span<const VerifiedEorSampler> slots);
} // namespace hulk::eor::gpu
