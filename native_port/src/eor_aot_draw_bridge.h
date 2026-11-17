// Link a REAL precompiled original Xbox shader pair to an EoR indexed draw.
// Vulkan shader-module/PSO creation is owned by the final host renderer, not
// by this source-only contract. No guest shader data is generated here.
#pragma once
#include "eor_native_frame.h"
#include "eor_spirv_bank.h"
#include "eor_effect_catalog.h"
#include "eor_shader_reflection.h"
#include "eor_verified_sampler_bindings.h"
#include <vector>
#include <span>

namespace hulk::eor::aot {
struct BoundIndexedDraw {
  native::IndexedDraw draw;
  Digest vertex_source_sha256{};
  Digest pixel_source_sha256{};
  uint32_t effect_hash = 0; // Original EoR ShaderEffect ID.
  uint32_t pass_index = UINT32_MAX; // Original candidate technique/pass slot.
  std::vector<gpu::VerifiedEorSampler> original_samplers;
  // Preserve original surface ownership even when different models share a PSO.
  uint32_t original_model_hash = 0;
  uint32_t original_surface_ordinal = UINT32_MAX;
};
class ShaderBindingBackend : public native::Backend {
 public:
  // An effect/pass alone is NOT a material owner. Refuse anonymous Draws.
  virtual bool BindOriginalSurface(uint32_t, uint32_t) {
    return false;
  }
  // True only after binding original effect material resources and PSO layout.
  // An effect hash alone is not proof of descriptor/vertex layout correctness.
  virtual bool BindOriginalEffect(uint32_t original_effect_hash,
                                  uint32_t pass_index) = 0;
  // The original float4 constant-register layouts, not Sonic's hardcoded
  // shader assumptions. Backend must bind actual game VS/PS constants.
  virtual bool BindOriginalConstants(const ReflectedShader& vertex,
                                     const ReflectedShader& pixel) = 0;
  // Called after selecting the correct native shader/PSO but before Draw.
  // Default fail-closed for nonempty sampler lists. Production implementations
  // must look up the EXACT original hash and Diligent shader resource variable,
  // bind resident SRVs, and commit resources to the active context.
  virtual bool BindOriginalSamplers(
      std::span<const gpu::VerifiedEorSampler> samplers) {
    return samplers.empty();
  }
  // Called before each DrawIndexed. Returns false if the Vulkan/Diligent
  // program cannot be created/selected for the real resource/vertex layout.
  virtual bool BindOfflineShaders(std::span<const uint32_t> vertex_spirv,
                                  std::span<const uint32_t> pixel_spirv) = 0;
  // Pass original shader identities directly to a native Vulkan backend.
  // Legacy implementations still use the existing bytecode interface.
  virtual bool BindVerifiedProgramPair(const Digest& vertex_digest,
                                       const Blob& vertex,
                                       const Digest& pixel_digest,
                                       const Blob& pixel) {
    if (vertex.stage!=Stage::Vertex || pixel.stage!=Stage::Pixel ||
        vertex_digest==pixel_digest) return false;
    return BindOfflineShaders(vertex.spirv,pixel.spirv);
  }
};
void ReplayAotScene(const native::FrameGeometry& frame,
                    std::span<const BoundIndexedDraw> draws,
                    const ShaderBank& bank, const EffectCatalog& effects,
                    const ReflectionBank& reflection, ShaderBindingBackend& backend);
}  // namespace hulk::eor::aot
