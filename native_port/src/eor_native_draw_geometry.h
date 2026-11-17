#pragma once
// Original game mesh + model + surface + shader-pair provenance gate.
// Does NOT discover the guest EoR draw ABI, shader sampler mapping, or PSO.
// Caller must obtain all binding evidence from verified original game state.
#include "eor_original_model_upload.h"
#include "eor_aot_draw_bridge.h"
#include "eor_xdk_draw_capture.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hulk::eor::native {
struct VerifiedDrawSource {
  uint32_t original_model_hash = 0;
  size_t original_mesh_header_offset = 0;
  uint32_t original_surface_ordinal = UINT32_MAX;
  // Own the material name: live XDK snapshots and Vulkan submissions can
  // outlive the source model catalogue's temporary string storage.
  std::string original_surface_name;
  uint32_t original_effect_hash = 0;
  uint32_t original_pass_index = UINT32_MAX;
  aot::Digest vertex_source_sha256{};
  aot::Digest pixel_source_sha256{};
  uint64_t vertex_shader_id = 0;
  uint64_t pixel_shader_id = 0;
  uint32_t first_index = 0;
  uint32_t index_count = 0;
  // Xbox XDK baseVertex needs explicit source->host rebase first.
  int32_t host_base_vertex = 0;
  // Explicit engine ABI, material ownership, converted host vertex layout and
  // fixed-function pass/depth/RT state. Never silently set these to true.
  bool engine_draw_verified = false;
  bool mesh_material_verified = false;
  bool host_vertex_layout_verified = false;
  bool render_state_verified = false;
  // Real guest material/fetch mapping supplied by a separately audited EoR
  // engine Draw, never inferred from adjacent textures in MODELS.ARC.
  std::vector<gpu::VerifiedEorSampler> original_sampler_slots;
};

// Per-Draw mapping: original USA PPC XDK method -> independently verified
// native EoR model/mesh/technique. Callsite alone is not material identity.
struct VerifiedXdkIndexedSubmission {
  xdk::IndexedXdkCall original_guest_call{};
  VerifiedDrawSource native_draw{};
  bool original_triangle_list_verified=false;
  bool guest_to_native_index_stream_verified=false;
  bool original_material_at_draw_verified=false;
};
std::vector<VerifiedDrawSource> MatchCapturedXdkDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkIndexedSubmission> source_verified);

// Genuine XDK capture to host Vulkan backend, when every exact mapping
// and sole-presenter GPU/camera resource is provided by the game runtime.
void ReplayCapturedXdkScene(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkIndexedSubmission> verified_submissions,
    const FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu);

// Build a native DrawIndexed batch only from real resident GPU buffers and
// exact per-mesh/per-material original engine mapping. No draw is submitted
// here: ReplayAotScene separately preflights SPIR-V and reflection before RT.
// Resident model objects and their GPU allocations must remain alive through
// submission AND the owning Vulkan completion fence.
std::vector<aot::BoundIndexedDraw> BindVerifiedGeometryDraws(
    std::span<const VerifiedDrawSource> sources,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::EffectCatalog& effects);

// Fully guarded orchestration: resolve real GPU buffers and original technique
// provenance, then use the existing AOT shader/constant/frame preflight.
// Requires the caller to have supplied a real ShaderBindingBackend with
// correct Vulkan render-target, depth, sampler and pipeline implementations.
void ReplayVerifiedGeometryScene(
    const FrameGeometry& frame,
    std::span<const VerifiedDrawSource> sources,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank, const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection, aot::ShaderBindingBackend& backend);
} // namespace hulk::eor::native
