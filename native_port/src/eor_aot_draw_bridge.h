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
// Real original guest VS register values at the exact source Draw.
// These are NOT automatically a Vulkan UBO or a 16:10 camera update:
 // the backend must still prove its own shader variable byte layout.
struct OriginalDrawVsMatrices {
  std::optional<xdk::MatrixRegisters> view_projection;
  std::optional<xdk::MatrixRegisters> world;
  std::optional<xdk::MatrixRegisters> previous_world;
  std::optional<xdk::MatrixRegisters> sky_view_projection;
};
std::optional<OriginalDrawVsMatrices> ExtractOriginalDrawVsMatrices(
    const ReflectedShader& vertex,const native::IndexedDraw& draw) noexcept;
// Compare the exact c7-c25 original XDK matrix shadow against the full
// 256*float4 VS capture from the SAME original PPC Draw. A shader cannot
// silently bind the previous object's World/ViewProj while PS parameters
// are taken from the current Draw. Full PS values remain accessible to
// the verified Vulkan uniform binder for exact source register reads.
bool ValidateCapturedOriginalShaderRegisters(
    const ReflectedShader& vertex,const native::IndexedDraw& draw) noexcept;
class ShaderBindingBackend : public native::Backend {
 public:
  // Stage all SOURCE-VERIFIED host pipeline and immutable GPU-buffer
  // dependencies BEFORE BeginScene. A Diligent/driver PSO failure must not
  // happen after the first geometry Draw has modified a live render target.
  // Legacy test backends may opt out; production Vulkan overrides this.
  virtual bool PreflightOriginalScene(std::span<const BoundIndexedDraw>) {
    return true;
  }
  // Full independent presentation must honor the ORIGINAL PPC XDK GPU
  // chronology around Draws (RT/depth binding, ClearF, tiled Resolve,
  // Present). Reject an unhandled command BEFORE BeginScene rather than
  // silently skip it and render corrupt color/depth/HUD.
  // Whole-frame capability gate also rejects dangerous source command
  // patterns such as mid-frame Resolve followed by more Draws, duplicate
  // front-buffer presents or unsupported EDRAM tiling/RT aliases.
  virtual bool CanReplayOriginalXdkTimeline(const xdk::Frame&) const {
    return false;
  }
  virtual bool CanReplayOriginalXdkCommand(
      const xdk::RawCommand&) const {return false;}
  // Called in original source order, on the same renderer thread as Draw.
  // Does NOT magically transform guest surface pointers into host views.
  virtual bool ReplayOriginalXdkCommand(
      const xdk::RawCommand&) {return false;}
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
                    const ReflectionBank& reflection, ShaderBindingBackend& backend,
                    const xdk::Frame* exact_original_timeline=nullptr);
}  // namespace hulk::eor::aot
