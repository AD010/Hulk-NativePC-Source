#pragma once
// Original Xbox USA Textures.ARC/TexStreams.ARC -> decoded GPU mip chain ->
// source-owned Vulkan SRV. No PM4, EDRAM, guest GPU texture alias or Xenia.
#include "eor_diligent_material_textures.h"
#include "eor_original_texture_assets.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <tuple>
#include <vector>
namespace Diligent { struct IRenderDevice; }
namespace hulk::eor::gpu {
struct VerifiedNativeMaterialImage {
  VerifiedEorSampler original_binding;
  OriginalTextureColorSpace source_proven_color_space=
      OriginalTextureColorSpace::Unknown;
  OriginalTextureViewKind shader_reflected_view=
      OriginalTextureViewKind::Native2D;
};
// Render-thread-only host GPU cache; original archive asset provider must
// outlive this object. Each material holds a strong reference to its host
// texture until the renderer drops that material AFTER its completion fence.
// Clear() drops only cache ownership; it cannot invalidate queued samplers.
class DiligentOriginalTextureResidency final {
 public:
  DiligentOriginalTextureResidency(
      Diligent::IRenderDevice* device,
      const textures::OriginalXboxTextureAssets& pinned_usa_assets);
  DiligentOriginalTextureResidency(const DiligentOriginalTextureResidency&)=delete;
  DiligentOriginalTextureResidency& operator=(const DiligentOriginalTextureResidency&)=delete;
  ~DiligentOriginalTextureResidency() noexcept;

  std::shared_ptr<const UploadedEorTexture> Acquire(
      uint32_t original_hash,
      OriginalTextureColorSpace source_proven_color_space,
      OriginalTextureViewKind shader_reflected_view);

  // Build a concrete per-DRAW set from verified material/fetch mappings.
  // Returns views owned by this cache, not borrowed guest memory.
  std::vector<OriginalDiligentSampler> BindMaterial(
      std::span<const VerifiedNativeMaterialImage> source_proven);

  std::size_t ResidentCount() const noexcept;
  void Clear() noexcept; // material pins survive until the caller releases them
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::gpu
