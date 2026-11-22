#pragma once
// Independent, engine-level EoR -> Vulkan DrawIndexed backend.
// There are NO Xenos registers, PM4 command packets or emulated EDRAM here.
// The caller must provide real EoR Draw provenance, decoded host vertices,
// original pass states and real camera/constant-buffer data.
#include "eor_aot_draw_bridge.h"
#include "eor_diligent_mesh_buffers.h"
#include "eor_diligent_scene_targets.h"
#include "eor_diligent_material_textures.h"
#include "eor_diligent_original_pipeline.h"
#include "eor_diligent_texture_residency.h"
#include "eor_original_model_upload.h"
#include "eor_native_font_geometry.h"
#include "eor_original_virtual_shader_identity.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
struct IShaderResourceBinding;
}
namespace hulk::eor::gpu {
struct NativeEorPassResources {
  // Original EoR material owner, not only a shared shader/effect pass.
  // Two surfaces may use identical shaders with DIFFERENT albedo/normal SRVs.
  uint32_t original_model_hash=0;
  uint32_t original_surface_ordinal=UINT32_MAX;
  VerifiedOriginalGraphicsPass pipeline;
  std::vector<OriginalDiligentSampler> original_textures;
};
// Actual original material pass and its precise per-shader image declarations.
// Passes are built from source-verified original resource ownership, never
// from nearby indexed texture candidate hashes.
struct VerifiedNativeMaterialPass {
  uint32_t original_model_hash=0;
  uint32_t original_surface_ordinal=UINT32_MAX;
  VerifiedOriginalGraphicsPass original_pipeline;
  std::vector<VerifiedNativeMaterialImage> original_images;
};
std::vector<NativeEorPassResources> AcquireVerifiedNativeMaterialPasses(
    std::span<const VerifiedNativeMaterialPass> verified,
    DiligentOriginalTextureResidency& textures);
// Slot 0 MUST be decoded float3 positions, additional slots MUST be
// float4 attributes uploaded only after genuine original BE decoding.
// The raw Xbox vertex stream is never usable as a Vulkan vertex buffer.
struct NativeEorDrawBuffers {
  uint64_t position_handle=0;
  uint64_t index_handle=0;
  uint32_t vertex_count=0;
  uint32_t index_count=0;
  std::vector<uint64_t> host_vertex_slots;
  // Exact source model/mesh provenance. Shared effects NEVER imply
  // that another model's physical index/vertex buffers may be used.
  uint32_t original_model_hash=0;
  size_t original_mesh_header_offset=SIZE_MAX;
  // Dynamic EFontShader only: the original guest geometry is transient
  // XDK VB/IB data, not an archived MODELS.ARC mesh/header offset.
  bool original_dynamic_font_verified=false;
  uint64_t original_font_frame=0;
  uint32_t original_font_draw_ordinal=UINT32_MAX;
  uint32_t original_font_atlas_hash=0;
};
// Build real immutable host XYZ/index buffer table from original uploaded
// Xbox360 models. Other shader input slots must be independently decoded.
std::vector<NativeEorDrawBuffers> PositionOnlyDrawBuffersFromOriginalModels(
    std::span<const model::UploadedOriginalModel* const> residents);
class DiligentEorDirectBackend final : public aot::ShaderBindingBackend {
 public:
  // The callback receives the CURRENT source-proven Draw, its actual
  // original frame and ordinal, and optional c7..c25 BE-decoded VS register
  // snapshot. This can be bound only when original shader reflection and
  // the exact SPIR-V constant-buffer variable layout have been validated.
  // It is NOT itself proof of a 16:10 projection hook or camera ownership.
  // The verified register values are passed as a typed payload; the caller
  // must supply the exact original SPIR-V UBO variable/packing semantics.
  // A per-pass callback cannot supply the right matrix for repeated models.
  using BindConstants = std::function<bool(
      Diligent::IShaderResourceBinding*,
      const aot::ReflectedShader&,const aot::ReflectedShader&,
      const native::IndexedDraw&,
      const aot::OriginalDrawVsMatrices&,
      const xdk::OriginalShaderRegisters&,
      std::span<const float> packed_vs,
      std::span<const float> packed_ps)>;
  using CompositeOutput = std::function<bool(
      Diligent::ITextureView*,
      native::Rect,native::Size)>;

  DiligentEorDirectBackend(
      Diligent::IRenderDevice* device,
      Diligent::IDeviceContext* context,
      DiligentSceneTargets& scene,
      DiligentMeshUploadBackend& resident_meshes,
      DiligentAotShaderModules& shaders,
      const aot::ShaderBank& shader_bank,
      const aot::EffectCatalog& original_effects,
      native::FrameGeometry frame,
      VerifiedSceneFormat formats,
      std::vector<NativeEorPassResources> original_passes,
      std::vector<NativeEorDrawBuffers> decoded_draw_buffers,
      BindConstants bind_verified_original_constants,
      CompositeOutput composite_native_scene);
  DiligentEorDirectBackend(const DiligentEorDirectBackend&)=delete;
  DiligentEorDirectBackend& operator=(const DiligentEorDirectBackend&)=delete;
  ~DiligentEorDirectBackend() noexcept override;
  // Sonic/Blue Dragon-style asset-load pipeline prewarming. Uses ONLY
  // resident original model/material owners and exact AOT shader SHA+PSO
  // state; no effect-name guesses or all-game brute-force compilation.
  // The constructor prewarms an initial bounded batch; callers uploading
  // more models can request further batches BEFORE the next GPU scene.
  // Returns the number of newly cached verified owner-specific pipelines.
  std::size_t PrewarmResidentOriginalPipelines(std::size_t max_new);
  // Stage the REAL EFontShader GPU geometry into the existing original
  // Vulkan pipeline BEFORE scene BeginScene. Builds one ordinary ordered
  // BoundIndexedDraw for ReplayAotScene, using original live VS/PS source
  // digests, exact source pass, checked font atlas, XDK constants and
  // immutable host float4/index32 buffers. Never manufactures a model.
  // The source-proven font resource hash must already own a matching
  // independently verified material pass in this backend.
  aot::BoundIndexedDraw StageVerifiedOriginalFontDraw(
      const xdk::RawDraw& source,
      const ui::VerifiedFontVertexLayout& original_layout,
      uint64_t source_frame,uint32_t source_ordinal,
      uint32_t original_font_resource_hash,uint32_t original_surface_ordinal,
      const aot::OriginalVirtualShaderIdentity& original_shaders);
  // Call ONLY after the host GPU completion fence for completed_frame.
  // An in-flight font VB/IB is never destroyed just because source Present
  // advanced or a later game frame started.
  void RetireCompletedOriginalFontDraws(uint64_t completed_frame) noexcept;
  bool PreflightOriginalScene(
      std::span<const aot::BoundIndexedDraw> draws) override;
  bool CanReplayOriginalXdkTimeline(const xdk::Frame& source) const override;
  bool CanReplayOriginalXdkCommand(
      const xdk::RawCommand& command) const override;
  bool ReplayOriginalXdkCommand(
      const xdk::RawCommand& command) override;

  bool BeginScene(native::Size world,uint32_t samples) override;
  void AbortScene() noexcept override;
  bool BindOriginalSurface(uint32_t model,uint32_t ordinal) override;
  bool BindOriginalEffect(uint32_t effect,uint32_t pass) override;
  bool BindOriginalConstants(const aot::ReflectedShader& vertex,
                             const aot::ReflectedShader& pixel) override;
  // Never accept anonymous bytecode lacking the original effect identity.
  bool BindOfflineShaders(std::span<const uint32_t>,
                          std::span<const uint32_t>) override {return false;}
  bool BindVerifiedProgramPair(const aot::Digest& vertex_digest,
                               const aot::Blob& vertex,
                               const aot::Digest& pixel_digest,
                               const aot::Blob& pixel) override;
  bool BindOriginalSamplers(
      std::span<const VerifiedEorSampler> samplers) override;
  bool DrawIndexed(const native::IndexedDraw& draw) override;
  bool ResolveScene(native::Size physical) override;
  bool Composite(native::Rect destination,native::Size backbuffer) override;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::gpu
