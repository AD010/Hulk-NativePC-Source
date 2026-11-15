// Link a REAL precompiled original Xbox shader pair to an EoR indexed draw.
// Vulkan shader-module/PSO creation is owned by the final host renderer, not
// by this source-only contract. No guest shader data is generated here.
#pragma once
#include "eor_native_frame.h"
#include "eor_spirv_bank.h"
#include "eor_effect_catalog.h"
#include "eor_shader_reflection.h"
#include <span>

namespace hulk::eor::aot {
struct BoundIndexedDraw {
  native::IndexedDraw draw;
  Digest vertex_source_sha256{};
  Digest pixel_source_sha256{};
  uint32_t effect_hash = 0; // Original EoR ShaderEffect ID.
  uint32_t pass_index = UINT32_MAX; // Original candidate technique/pass slot.
};
class ShaderBindingBackend : public native::Backend {
 public:
  // True only after binding original effect material resources and PSO layout.
  // An effect hash alone is not proof of descriptor/vertex layout correctness.
  virtual bool BindOriginalEffect(uint32_t original_effect_hash,
                                  uint32_t pass_index) = 0;
  // The original float4 constant-register layouts, not Sonic's hardcoded
  // shader assumptions. Backend must bind actual game VS/PS constants.
  virtual bool BindOriginalConstants(const ReflectedShader& vertex,
                                     const ReflectedShader& pixel) = 0;
  // Called before each DrawIndexed. Returns false if the Vulkan/Diligent
  // program cannot be created/selected for the real resource/vertex layout.
  virtual bool BindOfflineShaders(std::span<const uint32_t> vertex_spirv,
                                  std::span<const uint32_t> pixel_spirv) = 0;
};
void ReplayAotScene(const native::FrameGeometry& frame,
                    std::span<const BoundIndexedDraw> draws,
                    const ShaderBank& bank, const EffectCatalog& effects,
                    const ReflectionBank& reflection, ShaderBindingBackend& backend);
}  // namespace hulk::eor::aot
