#pragma once
#include "eor_xdk_draw_capture.h"
#include "eor_original_pause_vm.h"
#include "eor_original_camera_speed.h"
#include "eor_original_audio_menu.h"
#include "eor_usa_material_textures.h"
#include "eor_native_draw_geometry.h"
#include "eor_native_font_geometry.h"
#include "eor_original_shader_samplers.h"
#include "eor_original_virtual_shader_identity.h"
#include <functional>
#include <optional>
#include <rex/ppc/context.h>
#include <native_x_renderer/native_x_renderer.h>

namespace hulk::eor::gpu {class DiligentEorDirectBackend;class EorWin32VulkanHost;}
namespace hulk::eor::world {class NativeWorldStream;class OriginalMaterialCandidateIndex;}
namespace hulk::eor::xdk {
void DispatchOriginalPcMenu(PPCContext& ctx,uint8_t* base,PPCFunc* original,bool direct_method);
// Verified USA EoR script-VM global state_r (original guest 0x8264C798).
// Its r3 argument is the authentic original VM call context; the
// function is SHARED by unrelated game scripts. Read-only audit only:
// absolutely never open Windows Settings based on this common callback
// until the original UIPage_Pause_Settings owner/ABI is proven.
void ObserveOriginalEorVmState_r(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept;
// Called before/after the authentic menu VM boolean setter, even with
// Vulkan capture disabled. The original function remains authoritative.
std::optional<vm::OriginalCameraMenuChange> CaptureOriginalCameraMenuOption(
    uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept;
void FinishOriginalCameraMenuOption(
    std::optional<vm::OriginalCameraMenuChange> change,uint8_t* base) noexcept;
std::optional<vm::AudioMenuChange> CaptureOriginalAudioMenuOption(
    uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept;
void FinishOriginalAudioMenuOption(std::optional<vm::AudioMenuChange> change,
    uint8_t* base) noexcept;
void RestoreOriginalSubtitleOption(const PPCContext& ctx,uint8_t* base) noexcept;
std::optional<vm::CameraSpeedEvent> CaptureOriginalCameraSpeed(uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept;
void FinishOriginalCameraSpeed(std::optional<vm::CameraSpeedEvent> event,uint8_t* base) noexcept;
void RestoreOriginalCameraGetter(uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept;
void ObserveOriginalHavokStep(uint32_t method,const PPCContext& ctx,uint8_t* base) noexcept;
void ApplyOriginalHavokCatchup(const PPCContext& ctx,uint8_t* base) noexcept;
// Source-verified original 0x825F84C0 EoR material state slot to
// both original VS/PS XDK shader objects. 92-byte records and exact
// SetVertexShader/SetPixelShader handle sources are proven in USA PPC.
void ObserveOriginalEorShaderPass(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept;
// Genuine original USA Decal render-item material binding stage:
// 0x82642CB0 -> guest 0x825F8A50; the original r4 points to a
// 12-byte source material descriptor. Read-only invocation capture,
// verified against current Decal shader's original BE32(this+48).
// Never claim this is a ShaderEffects hash/pass or invoke the game twice.
void ObserveOriginalEorMaterialDescriptor(
    uint32_t original_method_guest,const PPCContext& original_ctx,
    uint8_t* original_guest_base) noexcept;
// RAII, read-only source EoR engine shader-method parent capture. The
// ORIGINAL USA method is always called through unchanged; this observes
// only r3/vtable and r4..r7 at entry so inner XDK Draws can retain their
// real source class invocation. No guessed model/material/shader witness.
class ScopedOriginalEorShaderMethod final {
 public:
  ScopedOriginalEorShaderMethod(
      uint32_t original_method_guest,const PPCContext& original_ctx,
      uint8_t* original_guest_base) noexcept;
  ~ScopedOriginalEorShaderMethod() noexcept;
  ScopedOriginalEorShaderMethod(const ScopedOriginalEorShaderMethod&)=delete;
  ScopedOriginalEorShaderMethod& operator=(
      const ScopedOriginalEorShaderMethod&)=delete;
 private:
  bool active_=false;
  // A rejected/re-entrant unverified original class method must mask
  // any enclosing source owner, never leak an outer shader's identity
  // onto an unrelated inner XDK Draw.
  bool suppressed_=false;
  bool touched_material_=false;
  uint8_t prior_shader_depth_=0;
  uint32_t prior_rejected_nesting_=0;
  uint32_t prior_shader_instance_=0;
  UsaAppliedMaterial prior_applied_material_{};
};
// Real original USA generic EoR 0x82642A88 dispatches shader
// virtual slot +44 at indirect PPC 0x82642BD4. Scoped to the exact
// original per-item method invocation; guest r3=this source shader,
// r4=source render item, r5=render context. This MUST NOT invent
// archive-model hashes/surface/material/texture identities.
class ScopedOriginalEorRenderItem final {
 public:
  ScopedOriginalEorRenderItem(
      uint32_t original_method_guest,const PPCContext& original_ctx,
      uint8_t* original_guest_base) noexcept;
  ~ScopedOriginalEorRenderItem() noexcept;
  ScopedOriginalEorRenderItem(const ScopedOriginalEorRenderItem&)=delete;
  ScopedOriginalEorRenderItem& operator=(
      const ScopedOriginalEorRenderItem&)=delete;
 private:
  bool touched_=false;
  uint8_t shader_source_depth_=0;
  uint32_t owning_shader_instance_=0;
  uint32_t previous_method_=0;
  uint32_t previous_item_=0;
  uint32_t previous_context_=0;
  uint32_t previous_item_vtable_=0;
  uint32_t previous_material_=0;
  bool previous_material_verified_=false;
  bool previous_verified_=false;
  UsaAppliedMaterial prior_applied_material_{};
};
bool CaptureEnabled();
void CapturePpcDraw(Kind kind, uint32_t guest, const PPCContext& ctx,
                    uint8_t* guest_base);
void CapturePpcCommand(CommandKind kind, uint32_t guest,
                       const PPCContext& ctx,uint8_t* guest_base);
void EndPpcHook(native_x_renderer::Call kind);
// The only completed read-only guest frame handoff to a source-verified
// native renderer. Callers must reject missing witnesses, missed frames,
// wrong resource ownership and incomplete shader bank BEFORE Vulkan BeginScene.
std::optional<DeliveredFrame> TakeCompletedPpcFrame();
// Render-thread entry after the genuine USA Present capture. It consumes
// ONLY a complete original mailbox frame and passes authentic engine
// model/material/VS/PS witnesses to the existing direct Vulkan backend.
// No automatic takeover: caller must prove sole host presenter and camera.
bool ReplayNextVerifiedPpcFrame(
    std::span<const native::VerifiedXdkResourceWitness> witnesses,
    const native::FrameGeometry& output,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& shaders,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& gpu,
    uint64_t expected_original_frame_sequence=0,
    bool* reached_gpu=nullptr);
// A source-verified, immutable per-Present snapshot, built by the actual
// EoR material/mesh owner. No automatic XDK register->EoR ownership guesses.
// All host buffers/PSO/SRV references and their source owners must stay live
// until the GPU completion fence; a missing producer means NativeX fallback.
struct VerifiedWorkerEorOwnerCatalog;
struct VerifiedWorkerEorFrame {
  // Immutable original USA Present sequence. Never replay resources or
  // materials from a different frame after an asynchronous GPU worker delay.
  uint64_t original_frame_sequence=0;
  native_x_renderer::Device* device=nullptr;
  native::FrameGeometry output{};
  std::vector<native::VerifiedXdkResourceWitness> witnesses;
  std::vector<std::shared_ptr<const model::UploadedOriginalModel>> residents;
  std::shared_ptr<const aot::ShaderBank> shaders;
  std::shared_ptr<const aot::EffectCatalog> effects;
  std::shared_ptr<const aot::ReflectionBank> reflection;
  std::shared_ptr<aot::ShaderBindingBackend> gpu;
  // Append so older verified-worker aggregate initializers keep their ABI.
  // A source sampler slot is not automatically an XDK texture fetch slot.
  std::shared_ptr<const aot::OriginalXdkShaderSamplers> samplers;
  // Index only: does NOT prove a guest shader-object -> virtual-header
  // ownership relation for an XDK Draw. Must not synthesize one.
  std::shared_ptr<const aot::OriginalVirtualShaderIdentity> virtual_ids;
  // Ties an automatically produced frame to the EXACT live source owner
  // catalogue generation. Manual per-frame witnesses leave this empty.
  std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> owner_catalog;
};
// Install only the exact private HEORVRT1 source-header index for
// Xenos-visible read-only Draw observation. This is deliberately independent
// of HEORSPV1, material uploads or any Vulkan output: source identities can
// be recovered before a complete compiled shader bank exists.
bool InstallOriginalShaderCaptureIndex(
    std::shared_ptr<const aot::OriginalVirtualShaderIdentity> original,
    std::shared_ptr<const aot::EffectCatalog> original_effects={}) noexcept;
void ClearOriginalShaderCaptureIndex() noexcept;
// Register the REAL private USA offline ShaderEffects bank, effect/pass index,
// and reflection together. The game owns the source generations; the GPU
// worker holds shared immutable references, never detached naked pointers.
// This does NOT attest any engine Draw, model identity or camera.
// Load all five private verified USA banks atomically from a local,
// user-owned directory. HEORSPV1 MUST contain real, fully compiled Xenos
// SPIR-V; HEORMAT2/HEORREF1/HEORSMP1/HEORVRT1 are metadata-only.
// This is called once before the Vulkan worker starts. No game assets
// or confidential shader program bytes are bundled in the public port.
bool InstallVerifiedSourceShaderPackFromDirectory(
    const std::filesystem::path& user_private_shader_dir) noexcept;
bool InstallVerifiedSourceShaderPack(
    std::shared_ptr<const aot::ShaderBank> shaders,
    std::shared_ptr<const aot::EffectCatalog> effects,
    std::shared_ptr<const aot::ReflectionBank> reflection,
    std::shared_ptr<const aot::OriginalXdkShaderSamplers> samplers = {},
    std::shared_ptr<const aot::OriginalVirtualShaderIdentity> virtual_ids = {});
bool HasVerifiedSourceShaderPack() noexcept;
// Source-only identity metadata permits live AOT demand before the first SPIR-V pair exists.
bool HasVerifiedOriginalShaderCaptureIndex() noexcept;
void ClearVerifiedSourceShaderPack() noexcept;
// Thread-safe source submission. A complete source-proven EoR engine owner
// must supply this per-frame; startup AOT shader banks alone are insufficient.
// The backend MUST use BindNativeEorVulkanBackbuffer, not Present itself.
bool InstallVerifiedWorkerEorFrame(
    std::shared_ptr<const VerifiedWorkerEorFrame> frame);
// A real original EoR Draw owner can now supply only its authenticated
// XDK resource witnesses and an existing NativeWorldStream::ResidentShared
// lookup: retain matching, fully uploaded Xbox model buffers from the
// same captured Present. No archive or shader source is guessed here.
// Invoke after USA Present capture, before the sole Vulkan worker's frame.
bool InstallVerifiedWorkerEorFrameWithResidents(
    std::shared_ptr<const VerifiedWorkerEorFrame> frame,
    const native::VerifiedOriginalResidentLookup& lookup);
// Persistent source-authenticated material/mesh catalogue. Register once,
// then the existing Vulkan worker binds those candidates to EACH actual
// original PPC Present without inventing a shader or guest buffer identity.
// The lookup is render-thread-only and must remain valid until Clear.
// Only candidates with independently verified original engine ownership
// are accepted. Dynamic EFontShader geometry still needs its own proven
// original live vertex-layout and cannot be substituted by an archive mesh.
struct VerifiedWorkerEorOwnerCatalog {
  native_x_renderer::Device* device=nullptr;
  native::FrameGeometry output{};
  std::vector<native::VerifiedXdkResourceWitness> verified_candidates;
  native::VerifiedOriginalResidentLookup resident_lookup;
  // Optional render-thread-only hook to NativeWorldStream::RequestSourceVerifiedModels.
  // A material candidate is a discovery HINT for original model I/O, not
  // permission to draw it. Only this Present's guest device, declaration,
  // shader objects, target pair and effect/pass select an upload request.
  // The resident lookup and ALL full Draw provenance checks still gate GPU.
  std::function<void(std::span<const uint32_t>)> request_source_models;
  std::shared_ptr<aot::ShaderBindingBackend> gpu;
  struct VerifiedFontMaterial {
    ui::VerifiedFontVertexLayout source_vertex_layout;
    // Before game initialization the original XDK vertex declaration
    // guest object is not allocated, and its pointer is per-session.
    // An independently source-verified EFont declaration resolver may
    // authenticate the CURRENT source Draw and fill its dynamic pointer
    // and actual VB stride/position/UV/color encoding on the worker.
    // Returning nullopt is a hard per-frame UI gate, not an invitation
    // to use a guessed 24-byte glyph vertex.
    std::function<std::optional<ui::VerifiedFontVertexLayout>(
        const xdk::RawDraw&)> resolve_verified_live_vertex_layout;
    // These identifiers must come from independently verified USA archive
    // font material and source XDK EFontShader, NOT a convenient UI image.
    uint32_t original_font_resource_hash=0;
    uint32_t original_surface_ordinal=UINT32_MAX;
    // The true ShaderEffects owner of this very material, proved from
    // both current XDK ShaderContainer headers. Multiple font materials
    // can share the same declaration and atlas but use different passes.
    uint32_t original_effect_hash=0;
    uint32_t original_pass_index=UINT32_MAX;
  };
  std::vector<VerifiedFontMaterial> verified_font_materials;
  // Must point at the SAME independently verified Vulkan backend as gpu.
  // Absent means dynamic font replay is not available and a real UI frame
  // falls back without presenting incomplete world-only graphics.
  std::shared_ptr<gpu::DiligentEorDirectBackend> direct_gpu;
  // Render-thread GPU upload step for source-authenticated world models.
  // NativeWorldStream::RequestSourceVerifiedModels queues CPU I/O but
  // Tick must be driven here before a Vulkan Draw can see new buffers.
  // Append to preserve older source-owner aggregate initialization order.
  // The owner supplies actual camera distance/LOD, never this bridge.
  std::function<void()> advance_source_model_uploads;
  // Exact user's original Xbox MODELS.ARC source with persistent async I/O
  // and physical GPU model residency; retained across Present epochs.
  std::shared_ptr<world::NativeWorldStream> source_models;
  // The original Xbox MODELS/Textures reverse hash index is a discovery
  // hint, never a fabricated model/material/PSO verification flag.
  std::shared_ptr<const world::OriginalMaterialCandidateIndex> source_material_candidates;
  // Original ShaderEffects and XDK Draw owner may publish a newly
  // authenticated material pass while a streamed city mesh loads. This
  // runs on the sole Vulkan worker, before scene preflight, and calls
  // DiligentEorDirectBackend::AdoptVerifiedOriginalMaterialPasses using
  // its own source-proven texture residency. No guessed material records.
  std::function<void(gpu::DiligentEorDirectBackend&,const Frame&)>
      publish_original_material_passes;
  // Built-in live producer uses the actual immutable current frame after
  // model I/O and before GPU replay. It returns every ordered Draw and
  // retains the real archive GPU model owners until the completion fence.
  std::function<std::vector<aot::BoundIndexedDraw>(
      const Frame&,std::vector<std::shared_ptr<const model::UploadedOriginalModel>>&)>
      prepare_live_original_draws;
  std::function<bool(const Frame&,native::Size)> accepts_live_source;
  // A valid live target graph differs from this scene, e.g. entering or
  // leaving the original pause menu. Do not wait a normal refresh period.
  std::function<bool()> needs_target_rebuild;
  // Optional built-in native GPU completion owner. It owns a real Vulkan
  // fence and retains staged original Draw/font buffers until GPU work
  // finishes; external source catalogues keep their existing safe path.
  std::function<bool(uint64_t)> submit_original_gpu_frame;
  // Full source-verified Draw sequence to the same native executor used
  // by the independent Win32 host. Current worker still owns physical Present.
  std::function<native_x_renderer::HulkEorPresentResult(
      const Frame&,std::span<const aot::BoundIndexedDraw>,
      std::span<const std::shared_ptr<const model::UploadedOriginalModel>>)>
      execute_original_frame;
};
bool InstallVerifiedWorkerEorOwnerCatalog(
    std::shared_ptr<const VerifiedWorkerEorOwnerCatalog> catalog);
// Build a resident, completely VERIFIED material/mesh/font catalog on the
// actual Vulkan worker when the NativeX device and source Present exist.
// Register this callback from the engine's original-asset material loader:
// it must check real archive owner, host PSOs, XDK screen and vertex layout.
// A null/unproved result never becomes render-eligible. The worker retries
// infrequently when newer original assets arrive, rather than requiring
// a hand-installed catalog for every current guest Present.
using VerifiedWorkerEorCatalogFactory=std::function<
    std::shared_ptr<const VerifiedWorkerEorOwnerCatalog>(
        native_x_renderer::Device&)>;
bool InstallVerifiedWorkerEorCatalogFactory(
    VerifiedWorkerEorCatalogFactory factory);
// Own only the user's game directory path. The GPU worker resolves the
// SHA-pinned USA original INDEX.IND and MODELS.ARC when it receives a
// real source-authenticated Vulkan material/PSO owner catalogue.
void InstallOriginalEorGameAssetRoot(
    const std::filesystem::path& user_original_game_root);
void ClearOriginalEorGameAssetRoot() noexcept;
void InstallOriginalEorMaterialCandidateIndexPath(
    const std::filesystem::path& private_source_index);
void ClearVerifiedWorkerEorCatalogFactory() noexcept;
void ClearVerifiedWorkerEorOwnerCatalog() noexcept;
void ClearVerifiedWorkerEorFrame() noexcept;
// Exact native EoR Vulkan *backbuffer completion*, not the guest XDK frame
// count and not proof the host GPU swapchain has finished Present.
struct EorWorkerStatus {
  uint64_t ready_backbuffers=0;
  uint64_t failed_after_gpu=0;
};
EorWorkerStatus GetEorWorkerStatus() noexcept;
// Source-only per-Present coverage. This is a read-only original XDK
// counter, NOT a Vulkan rendering readiness score; one present may contain
// unverified Draws, unknown model owners or uncompiled shader modules.
struct OriginalEorSourceCoverage {
  uint64_t original_frame_sequence=0;
  uint32_t original_xdk_draws=0;
  uint32_t original_vs_headers=0;
  uint32_t original_ps_headers=0;
  uint32_t original_live_vb0=0;
  uint32_t original_live_ib=0;
  uint32_t original_indexed_with_exact_buffer_bytes=0;
  uint32_t original_vs_ps_pairs=0;
  uint32_t original_effect_passes=0;
  bool complete_xdk_timeline=false;
};
OriginalEorSourceCoverage GetOriginalEorSourceCoverage() noexcept;
#ifdef _WIN32
// UI-thread entry points. post must enqueue asynchronously onto that thread.
bool StartExclusiveEorFrames(std::unique_ptr<gpu::EorWin32VulkanHost> host,
    std::function<bool(std::function<void()>)> post) noexcept;
void StopExclusiveEorFrames() noexcept;
#endif
native_x_renderer::HulkEorPresentResult TryWorkerEorFrame(
    native_x_renderer::Device& device) noexcept;
}
