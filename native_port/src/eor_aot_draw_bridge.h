// Link a REAL precompiled original Xbox shader pair to an EoR indexed draw.
// Vulkan shader-module/PSO creation is owned by the final host renderer, not
// by this source-only contract. No guest shader data is generated here.
#pragma once
#include "eor_native_frame.h"
#include "eor_spirv_bank.h"
#include "eor_effect_catalog.h"
#include <span>

namespace hulk::eor::aot {
struct BoundIndexedDraw {
  native::IndexedDraw draw;
  Digest vertex_source_sha256{};
  Digest pixel_source_sha256{};
  uint32_t effect_hash = 0; // Verified original EoR ShaderEffect ID, never guest pointer.
};
class ShaderBindingBackend : public native::Backend {
 public:
  // True only after binding original effect material resources and PSO layout.
  // An effect hash alone is not proof of descriptor/vertex layout correctness.
  virtual bool BindOriginalEffect(uint32_t original_effect_hash) = 0;
  // Called before each DrawIndexed. Returns false if the Vulkan/Diligent
  // program cannot be created/selected for the real resource/vertex layout.
  virtual bool BindOfflineShaders(std::span<const uint32_t> vertex_spirv,
                                  std::span<const uint32_t> pixel_spirv) = 0;
};
void ReplayAotScene(const native::FrameGeometry& frame,
                    std::span<const BoundIndexedDraw> draws,
                    const ShaderBank& bank, const EffectCatalog& effects,
                    ShaderBindingBackend& backend);
}  // namespace hulk::eor::aot
