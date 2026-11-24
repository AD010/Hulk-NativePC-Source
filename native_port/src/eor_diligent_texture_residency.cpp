#include "eor_diligent_texture_residency.h"
#include "RenderDevice.h"
#include "RefCntAutoPtr.hpp"
#include <stdexcept>
#include <mutex>
#include <tuple>
#include <utility>

namespace hulk::eor::gpu {
struct DiligentOriginalTextureResidency::State {
  State(Diligent::IRenderDevice* d,
        const textures::OriginalXboxTextureAssets& owner)
      :device(d),assets(owner){}
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  const textures::OriginalXboxTextureAssets& assets;
  // Texture cache is shared by loading/UI and source Draw preparation.
  // Hold this across both lookup and allocation to prevent duplicate GPU
  // images and invalidated map iterators when different threads request
  // the same original atlas at once.
  mutable std::mutex residency_mutex;
  using Key=std::tuple<uint32_t,OriginalTextureColorSpace,OriginalTextureViewKind>;
  // Host images can remain bound in materials even after LOD cache eviction.
  std::map<Key,std::shared_ptr<const UploadedEorTexture>> textures;
};
DiligentOriginalTextureResidency::DiligentOriginalTextureResidency(
    Diligent::IRenderDevice* device,
    const textures::OriginalXboxTextureAssets& pinned_usa_assets)
    :state_(device ? std::make_unique<State>(device,pinned_usa_assets) : nullptr) {
  if(!state_ || device->GetDeviceInfo().Type!=Diligent::RENDER_DEVICE_TYPE_VULKAN)
    throw std::invalid_argument("Only native Vulkan EoR original texture residency supported");
}
DiligentOriginalTextureResidency::~DiligentOriginalTextureResidency() noexcept = default;
std::shared_ptr<const UploadedEorTexture> DiligentOriginalTextureResidency::Acquire(
    uint32_t hash, OriginalTextureColorSpace space,OriginalTextureViewKind view) {
  if(!hash || space==OriginalTextureColorSpace::Unknown ||
     (view!=OriginalTextureViewKind::Native2D &&
      view!=OriginalTextureViewKind::NativeX2DArray &&
      view!=OriginalTextureViewKind::Native1D &&
      view!=OriginalTextureViewKind::NativeCube))
    throw std::invalid_argument("Original EoR material color space/image dimension not proven");
  const State::Key key{hash,space,view};
  std::lock_guard lock(state_->residency_mutex);
  const auto old=state_->textures.find(key);
  if(old!=state_->textures.end())return old->second;
  // Direct original resource loader verifies the pinned USA IND, LFXT
  // inline extent, real TexStreams mip sizes and source/format layout.
  auto original=state_->assets.LoadHash(hash);
  if(!original || !original->pinned_usa_index ||
     (view==OriginalTextureViewKind::NativeCube ? !original->verified_cube :
       (!original->verified_gpu_mips || !original->original_mip_plan ||
        !original->original_mip_plan->absolute_source_offsets_proven)))
    throw std::invalid_argument("Original USA EoR texture lacks complete source-proven mip chain");
  auto image=UploadVerifiedOriginalTexture(state_->device.RawPtr(),
                                           *original,space,view);
  if(image.original_texture_hash!=hash || !image.SRV() ||
     !image.original_mip_levels || image.view_kind!=view)
    throw std::runtime_error("Original EoR texture upload produced incompatible host SRV");
  auto holder=std::make_shared<const UploadedEorTexture>(std::move(image));
  auto [it,inserted]=state_->textures.emplace(key,std::move(holder));
  if(!inserted)throw std::logic_error("EoR source residency key collision");
  return it->second;
}
std::vector<OriginalDiligentSampler>
DiligentOriginalTextureResidency::BindMaterial(
    std::span<const VerifiedNativeMaterialImage> declared) {
  std::vector<VerifiedEorSampler> identities;
  identities.reserve(declared.size());
  for(const auto& entry:declared) {
    if(!entry.original_binding.original_texture_format_and_mips_verified ||
       entry.source_proven_color_space==OriginalTextureColorSpace::Unknown)
      throw std::invalid_argument("EoR material has unverified shader texture declaration");
    identities.push_back(entry.original_binding);
  }
  ValidateOriginalEorSamplers(identities);
  // Fail-safe: no shader variables are mutated in this step. Uploads are
  // resident for reuse; actual SRB binding happens only after ALL images pass.
  std::vector<OriginalDiligentSampler> result;
  result.reserve(declared.size());
  for(const auto& entry:declared) {
    OriginalDiligentSampler sampler;
    sampler.identity=entry.original_binding;
    sampler.required_view=entry.shader_reflected_view;
    sampler.uploaded=Acquire(entry.original_binding.original_texture_hash,
                             entry.source_proven_color_space,
                             entry.shader_reflected_view);
    result.push_back(std::move(sampler));
  }
  return result;
}
std::size_t DiligentOriginalTextureResidency::ResidentCount() const noexcept {
  if(!state_)return 0;
  std::lock_guard lock(state_->residency_mutex);
  return state_->textures.size();
}
void DiligentOriginalTextureResidency::Clear() noexcept {
  if(!state_)return;
  std::lock_guard lock(state_->residency_mutex);
  state_->textures.clear();
}
} // namespace hulk::eor::gpu
