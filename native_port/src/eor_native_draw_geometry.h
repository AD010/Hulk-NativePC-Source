#pragma once
// Original game mesh + model + surface + shader-pair provenance gate.
// Does NOT discover the guest EoR draw ABI, shader sampler mapping, or PSO.
// Caller must obtain all binding evidence from verified original game state.
#include "eor_original_model_upload.h"
#include "eor_aot_draw_bridge.h"
#include "eor_xdk_draw_capture.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
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
  // Resolved at the original XDK hook before Guest memory advances.
  uint64_t original_frame_sequence = 0;
  uint32_t original_call_ordinal = UINT32_MAX;
  std::optional<xdk::OriginalVsMatrices> original_vs_matrices;
  std::shared_ptr<const xdk::OriginalShaderRegisters> original_shader_constants;
  // Explicit engine ABI, material ownership, converted host vertex layout and
  // fixed-function pass/depth/RT state. Never silently set these to true.
  bool engine_draw_verified = false;
  bool mesh_material_verified = false;
  bool host_vertex_layout_verified = false;
  bool render_state_verified = false;
  // Set only by MatchCapturedMixedDraws after proving original DrawVertices
  // is a sequential indexed host mesh window with an exact guest owner.
  bool original_linear_draw_verified = false;
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

// Source-proven conversion for the second genuine USA draw ABI:
// DrawVertices is non-indexed at the XDK boundary, but MAY reuse an original
// uploaded host indexed triangle-list only if the full native draw window is
// exactly sequential and its original guest stream mapping is verified.
struct VerifiedXdkVerticesSubmission {
  xdk::VerticesXdkCall original_guest_call{};
  VerifiedDrawSource native_draw{};
  uint32_t host_first_vertex=0;
  bool original_triangle_list_verified=false;
  bool guest_to_native_vertex_stream_verified=false;
  bool original_material_at_draw_verified=false;
};
using VerifiedXdkDrawSubmission=
    std::variant<VerifiedXdkIndexedSubmission,VerifiedXdkVerticesSubmission>;
// True PPC guest object identities observed at the exact Draw. They are
// not original archive hashes, not Vulkan handles, and must have a proven
// model/material/technique correspondence before GPU submission.
struct OriginalXdkResourceIdentity {
  uint32_t device=0,vertex_declaration=0,index_buffer=0;
  uint32_t vertex_shader=0,pixel_shader=0,rt0=0,depth=0;
  std::array<uint32_t,16> textures{};
  bool operator==(const OriginalXdkResourceIdentity&) const = default;
};
OriginalXdkResourceIdentity ResourcesAtDraw(
    const xdk::OriginalVsMatrices& snapshot) noexcept;
struct VerifiedXdkResourceWitness {
  OriginalXdkResourceIdentity original_resources;
  VerifiedXdkDrawSubmission original_source_submission;
  // Only shader-read, source-proven guest texture slots participate in
  // material identity. XDK retains unrelated textures in unused slots;
  // comparing all 16 falsely rejects valid Draws on every state change.
  // This mask is an independently verified ORIGINAL XDK SetTexture slot
  // mapping, not a guess from Xenos fetch index or archive hash.
  uint16_t source_verified_texture_slot_mask=0;
  bool original_xdk_texture_slot_mapping_verified=false;
  bool original_guest_resource_to_material_mapping_verified=false;
  // Optional SHA/guest-memory verified proof that the original XDK indexed
  // stream is the SAME complete original source buffer as the host mesh,
  // with a linear vertex/index origin. Enables repeated DrawIndexedVertices
  // with different firstIndex/baseVertex/count using the real live values.
  // Never infer this proof from shared shaders, LR, or resource pointers.
  bool original_linear_index_stream_verified=false;
};
// Resolve the entire captured frame with independently verified original
// model/mesh/pass witnesses. This does NOT discover missing witnesses.
// It rejects unrecognized/ambiguous Draws before any GPU scene starts.
std::vector<VerifiedDrawSource> ResolveCapturedResourceDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkResourceWitness> original_witnesses,
    std::span<const model::UploadedOriginalModel* const> residents);
void ReplayCapturedResourceScene(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkResourceWitness> original_witnesses,
    const FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu);
std::vector<VerifiedDrawSource> MatchCapturedMixedDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkDrawSubmission> source_verified,
    std::span<const model::UploadedOriginalModel* const> residents);
void ReplayCapturedMixedScene(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkDrawSubmission> verified_submissions,
    const FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu);

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
