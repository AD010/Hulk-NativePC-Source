#pragma once
// Original game mesh + model + surface + shader-pair provenance gate.
// Does NOT discover the guest EoR draw ABI, shader sampler mapping, or PSO.
// Caller must obtain all binding evidence from verified original game state.
#include "eor_original_model_upload.h"
#include "eor_aot_draw_bridge.h"
#include "eor_xdk_draw_capture.h"
#include <cstddef>
#include <cstdint>
#include <functional>
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
  // Optional original XDK ShaderObject -> ORIGINAL guest ShaderContainer
  // header evidence for the exact Draw. The producer must actually
  // capture these bytes through its independently verified original PPC
  // object/header pointer ABI. No 52-class name or pass-index guessing.
  // When HEORVRT1 is loaded, the Vulkan worker requires BOTH proofs.
  std::optional<aot::OriginalLiveXdkShaderHeaderWitness>
      original_live_vertex_shader_header;
  std::optional<aot::OriginalLiveXdkShaderHeaderWitness>
      original_live_pixel_shader_header;
  // Explicit engine ABI, material ownership, converted host vertex layout and
  // fixed-function pass/depth/RT state. Never silently set these to true.
  bool engine_draw_verified = false;
  bool mesh_material_verified = false;
  bool host_vertex_layout_verified = false;
  bool render_state_verified = false;
  // Only real guest triangle-list or triangle-fan DrawVertices whose full
  // indexed host mesh window was verified vertex-by-vertex can set this.
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
  // Real source primitive 4 or 6 must exactly match a HOST triangle-list
  // index window and independently proven material/vertex-stream ownership.
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
  // Captured original XDK SetStreamSource, not inferred mesh identifiers.
  std::array<uint32_t,16> vertex_buffers{};
  bool operator==(const OriginalXdkResourceIdentity&) const = default;
};
OriginalXdkResourceIdentity ResourcesAtDraw(
    const xdk::OriginalVsMatrices& snapshot) noexcept;
struct ExactOriginalGuestMesh {
  uint32_t model_hash=0;
  size_t mesh_header_offset=SIZE_MAX;
  bool original_index_big_endian=false;
  // Origin of the exact guest source mesh INSIDE the larger live XDK
  // allocations. The caller must still verify its shader/material/surface.
  uint32_t guest_first_vertex=0,guest_first_index=0;
  // Original Xbox index width, source-proven by the complete bound
  // GPU allocation. The host still uploads native uint32 DrawIndexed.
  uint8_t original_index_element_bytes=0;
};
// Source-proven model and mesh ONLY; the independently observed EoR shader,
// material/surface and camera remain separate mandatory ownership gates.
std::optional<ExactOriginalGuestMesh> IdentifyExactOriginalGuestMesh(
    const xdk::RawDraw& draw,
    std::span<const model::UploadedOriginalModel* const> residents);
// Enforce original ShaderEffects digest identity against real guest XDK
// shader objects on EVERY Draw before the native EoR Vulkan pass begins.
void ValidateLiveOriginalShaderPasses(
    const xdk::Frame& original_frame,
    std::span<const VerifiedDrawSource> original_draws,
    const aot::EffectCatalog& effects);
// Material surface identity from independently verified source shader sampler
// texture hashes and exact original indexed EoR model material records.
std::optional<uint32_t> IdentifyOriginalMaterialSurface(
    const model::UploadedOriginalModel& original_model,
    std::span<const gpu::VerifiedEorSampler> live_sampler_evidence) noexcept;
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
  // The ORIGINAL vertex declaration's actually used source slots.
  // Never assume stale bound streams are consumed by the active shader.
  uint16_t source_verified_vertex_stream_slot_mask=0;
  bool original_xdk_vertex_stream_slot_mapping_verified=false;
  bool original_guest_resource_to_material_mapping_verified=false;
  // Optional SHA/guest-memory verified proof that the original XDK indexed
  // stream is the SAME complete original source buffer as the host mesh,
  // with a linear vertex/index origin. Enables repeated DrawIndexedVertices
  // with different firstIndex/baseVertex/count using the real live values.
  // Never infer this proof from shared shaders, LR, or resource pointers.
  bool original_linear_index_stream_verified=false;
  // Read-only original ENGINE shader method parent, sampled from the
  // same thread and exact original XDK Draw before the guest GPU API.
  // It is attached to INDEPENDENT original material/mesh witnesses, and
  // never substitutes for that original owner verification.
  std::optional<xdk::OriginalEorShaderMethodAtDraw>
      original_source_engine_shader_method;
};
// Pair already verified EoR material/mesh candidate witnesses with their
// EXACT original PPC shader class method at Draw, if one was observed.
// Never set engine_draw_verified or original material/texture/PSO flags.
// Multiple candidate material owners may share one original shader method;
// downstream resolution still rejects ambiguity or missing material proofs.
std::vector<VerifiedXdkResourceWitness> AttachLiveEorShaderMethodParents(
    const xdk::Frame& exact_original,
    std::span<const VerifiedXdkResourceWitness> material_owner_witnesses);
// The actual original GPU archive streamer owns strong uploaded Vulkan
// positions, indices and source-decoded attribute buffers. Retain only
// those real models whose engine-verified Draws match the original XDK
// timeline and source model+mesh+surface identity. A caller can bind its
// NativeWorldStream::ResidentShared lookup without manufacturing pointers.
// Missing, stale, ambiguous or incomplete source owners reject the frame.
using VerifiedOriginalResidentLookup=std::function<
    std::shared_ptr<const model::UploadedOriginalModel>(uint32_t)>;
std::vector<std::shared_ptr<const model::UploadedOriginalModel>>
RetainVerifiedOriginalFrameResidents(
    const xdk::Frame& source,
    std::span<const VerifiedXdkResourceWitness> original_witnesses,
    const VerifiedOriginalResidentLookup& original_residents);
// Resolve the entire captured frame with independently verified original
// model/mesh/pass witnesses. This does NOT discover missing witnesses.
// It rejects unrecognized/ambiguous Draws before any GPU scene starts.
std::vector<VerifiedDrawSource> ResolveCapturedResourceDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkResourceWitness> original_witnesses,
    std::span<const model::UploadedOriginalModel* const> residents,
    std::vector<VerifiedXdkResourceWitness>* selected_live_witnesses=nullptr);
// Convert reusable original engine material candidates into EXACT per-Draw
// GPU witnesses. Each accepted candidate must independently establish its
// model, surface, mesh, sampler and shader identity, then survive byte-exact
// comparison with the current Xbox buffer generation and shader objects.
// Nothing missing is fabricated: ambiguous or unowned Draws reject the frame.
std::vector<VerifiedXdkResourceWitness> MaterializeLiveOriginalResourceWitnesses(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkResourceWitness> verified_candidates,
    std::span<const model::UploadedOriginalModel* const> residents);
void ReplayCapturedResourceScene(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkResourceWitness> original_witnesses,
    const FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu,
    bool* scene_begin_attempted=nullptr);
// Exact original Present mailbox -> native EoR draw/material/shader/host GPU.
// This does not manufacture source owners and rejects any dropped frames;
// caller MUST already own one verified Vulkan device/swapchain/presenter.
void ReplayDeliveredResourceScene(
    const xdk::DeliveredFrame& delivered,
    std::span<const VerifiedXdkResourceWitness> original_witnesses,
    const FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu,
    bool* scene_begin_attempted=nullptr);
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
