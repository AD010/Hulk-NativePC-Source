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
#include "eor_original_model_upload.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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
};
// Build real immutable host XYZ/index buffer table from original uploaded
// Xbox360 models. Other shader input slots must be independently decoded.
std::vector<NativeEorDrawBuffers> PositionOnlyDrawBuffersFromOriginalModels(
    std::span<const model::UploadedOriginalModel* const> residents);
class DiligentEorDirectBackend final : public aot::ShaderBindingBackend {
 public:
  // The callback receives the CURRENT source-proven Draw. The same
  // EoR effect/VS/PS can render different models with different World and
  // ViewProj registers; a per-pass callback cannot supply the right matrix.
  using BindConstants = std::function<bool(
      Diligent::IShaderResourceBinding*,
      const aot::ReflectedShader&,const aot::ReflectedShader&,
      const native::IndexedDraw&)>;
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
