// Link a REAL precompiled original Xbox shader pair to an EoR indexed draw.
// Vulkan shader-module/PSO creation is owned by the final host renderer, not
// by this source-only contract. No guest shader data is generated here.
#pragma once
#include "eor_original_virtual_shader_identity.h"
#include "eor_native_frame.h"
#include "eor_spirv_bank.h"
#include "eor_effect_catalog.h"
#include "eor_shader_reflection.h"
#include "eor_verified_sampler_bindings.h"
#include "eor_xdk_draw_capture.h"
#include <vector>
#include <cstddef>
#include <cstdint>
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
  // Exact original MODELS.ARC mesh-header identity. A model hash alone
  // cannot distinguish meshes using different vertex/index layouts.
  size_t original_mesh_header_offset = SIZE_MAX;
  // True ONLY after a verified original DrawVertices stream was checked
  // against sequential decoded host triangle indices by the mesh matcher.
  bool source_nonindexed_linearization_verified = false;
  // Source-verified EFontShader transient original GPU geometry, keyed by
  // THIS real Draw and its original font resource/atlas. No invented
  // MODELS.ARC mesh offset may be required for this variant.
  bool original_dynamic_font_draw = false;
  // In this variant original_model_hash is the exact live material context
  // guest pointer, and original_surface_ordinal is the source Draw ordinal.
  // It belongs to the transient runtime owner namespace, not MODELS.ARC.
  bool original_runtime_draw=false;
  std::optional<OriginalPixelSamplerControls> original_pixel_sampling{};
  bool original_direct_xdk_runtime=false;
  bool original_point_list=false;
  std::optional<OriginalVertexSamplerControls> original_vertex_sampling{};
};
inline bool ProvesOriginalVertexTextureBindings(const xdk::RawDraw& source,
    std::span<const gpu::VerifiedEorSampler> samplers,const Digest* expected=nullptr) noexcept {
  const auto* policy=source.observed_live_vertex_shader_header &&
      source.observed_live_vertex_shader_header->vertex_sampling ?
      &*source.observed_live_vertex_shader_header->vertex_sampling : nullptr;
  if(policy && expected && policy->source!=*expected)return false;
  uint8_t seen=0;
  for(const auto& image:samplers) {
    if(image.stage!=gpu::OriginalSamplerStage::Vertex)continue;
    if(!gpu::IsOriginalScalarVertexTexture(image) || !policy ||
       !source.observed_vs_matrices || !source.observed_shader_constants ||
       !image.original_material_ownership_verified || !image.original_shader_fetch_slot_verified ||
       !image.original_texture_format_and_mips_verified ||
       bool(image.original_texture_hash)==bool(image.original_resolved_guest_object))return false;
    const auto slot=image.original_fetch_register;
    const auto bit=static_cast<uint8_t>(1u<<slot);
    const auto bound=source.observed_vs_matrices->original_vertex_texture_guest_objects[slot];
    if(!bound || (seen&bit) || !(policy->sampled_slots&bit) ||
       !(source.observed_shader_constants->valid_vertex_fetch&bit))return false;
    if(image.original_resolved_guest_object) {
      if(bound!=image.original_resolved_guest_object)return false;
    } else {
      const auto& owner=source.original_vertex_texture_owners[slot];
      if(!owner.asset || !owner.wrapper || owner.xdk!=bound ||
         owner.hash!=image.original_texture_hash)return false;
    }
    seen|=bit;
  }
  return seen==(policy?policy->sampled_slots:0u);
}

// A UI/post-process draw can bind original programs outside the engine's
// observed material method. The exact device+Draw ordinal is then a
// transient owner, not an archive hash. Admit only exact original
// Resolve inputs or an independently observed and revalidated ERTexture
// -> wrapper -> current bound XDK texture identity.
inline bool ProvesDirectXdkRuntimeBindings(const xdk::RawDraw& source,
    std::span<const gpu::VerifiedEorSampler> samplers,uint32_t owner) noexcept {
  if(!source.observed_vs_matrices || !source.observed_shader_constants ||
     !source.observed_shader_constants->fixed_graphics_state ||
     !source.observed_live_vertex_shader_header || !source.observed_live_pixel_shader_header ||
     !source.observed_live_eor_effect_pass ||
     !source.observed_live_pixel_shader_header->pixel_sampling ||
     !source.captured_vertex_bindings || !source.vertex_declaration ||
     !owner || owner!=source.gpr[0] ||
     owner!=source.observed_vs_matrices->original_guest_device ||
     (!xdk::DecodeOriginalVerticesXdkCall(source) && !xdk::DecodeOriginalIndexedXdkCall(source)))
    return false;
  const auto& live=*source.observed_vs_matrices;
  // Known engine wrapper calls may take the direct runtime path only
  // when their original geometry owner was actually authenticated.
  if(source.lr==0x826676A8u || source.lr==0x8268BFE4u) {
    const auto& geometry=source.original_geometry_wrapper;
    if(!geometry || !geometry->geometry || !geometry->engine ||
       geometry->device!=owner || geometry->vertex!=live.original_vertex_buffer_guest_objects[0] ||
       geometry->declaration!=live.original_vertex_declaration_guest_object ||
       (!geometry->screen_quad && geometry->index!=live.original_index_buffer_guest_object))
      return false;
  }
  if(!ProvesOriginalVertexTextureBindings(source,samplers))return false;
  const auto& controls=*source.observed_live_pixel_shader_header->pixel_sampling;
  uint16_t seen=0;
  for(const auto& image:samplers) {
    if(!image.original_runtime_material || image.original_model_hash!=owner ||
       image.original_effect_hash!=source.observed_live_eor_effect_pass->effect_hash ||
       image.original_pass_index!=source.observed_live_eor_effect_pass->pass_index)
      return false;
    if(image.stage==gpu::OriginalSamplerStage::Vertex)continue;
    if(image.stage!=gpu::OriginalSamplerStage::Pixel || image.original_fetch_register>=16u)return false;
    const auto slot=image.original_fetch_register;
    const auto bound=live.original_texture_guest_objects[slot];
    if(!bound || bool(image.original_texture_hash)==bool(image.original_resolved_guest_object))return false;
    if(image.original_resolved_guest_object) {
      if(bound!=image.original_resolved_guest_object)return false;
    } else {
      const auto& witness=source.original_pixel_texture_owners[slot];
      if(!witness.asset || !witness.wrapper || witness.xdk!=bound ||
         witness.hash!=image.original_texture_hash ||
         source.original_pixel_texture_keys[slot]!=witness.hash)return false;
    }
    const auto bit=uint16_t(1u<<slot);
    if((seen&bit) || !(source.observed_shader_constants->valid_pixel_fetch&bit))return false;
    seen|=bit;
  }
  return seen==controls.sampled_slots;
}
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
  virtual bool BindOriginalRuntimeSurface(uint32_t,uint32_t) {return false;}
  virtual bool BindOriginalSurface(uint32_t, uint32_t) {
    return false;
  }
  // True only after binding original effect material resources and PSO layout.
  // An effect hash alone is not proof of descriptor/vertex layout correctness.
  virtual bool BindOriginalEffect(uint32_t original_effect_hash,
                                  uint32_t pass_index) = 0;
  // The original float4 constant-register layouts, not Sonic's hardcoded
  // shader assumptions. Backend must bind actual game VS/PS constants.
  // Explicitly absent PS: no synthetic effect or pixel shader digest.
  virtual bool BindOriginalVertexOnlyProgram(const Digest&,const Blob&,
                                              const ReflectedShader&) {return false;}
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
                    const xdk::Frame* exact_original_timeline=nullptr,
                    bool* scene_begin_attempted=nullptr);
}  // namespace hulk::eor::aot
