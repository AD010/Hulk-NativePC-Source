#pragma once
// True original-EoR model I/O worker -> host-GPU upload on caller/render thread.
// Never issues Xenos commands, invents a surface-to-mesh mapping or replaces
// a still-in-flight GPU buffer. Client owns verified game distance/LOD tiers.
#include "eor_original_model_upload.h"
#include "eor_native_lod.h"
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <thread>
namespace hulk::eor::world {
class ModelSource {
 public:
  virtual ~ModelSource() = default;
  // Called ONLY by I/O worker; must not touch Diligent or guest PPC memory.
  virtual std::optional<model::LoadedNativeGeometry> Load(uint32_t original_hash) = 0;
};
class NativeWorldStream {
 public:
  NativeWorldStream(std::unique_ptr<ModelSource> source,
                    model::NativeMeshUploadBackend& backend,
                    std::vector<LodTier> verified_tiers,
                    LodPolicy policy = {});
  NativeWorldStream(const NativeWorldStream&) = delete;
  NativeWorldStream& operator=(const NativeWorldStream&) = delete;
  ~NativeWorldStream() noexcept;
  // Call on SAME GPU/render thread each frame. One staged asset at most is
  // uploaded per tick; I/O never blocks this method. GPU upload itself may
  // consume time and should be submitted before the scene's DrawIndexed.
  LodDecision Tick(double camera_distance_world_units);
  // Pure world material stream: no source-proven Hulk/city LOD tiers are
  // required for models explicitly authenticated by their live Draw owner.
  // Unlike Tick(distance), this never invents camera distance or LOD.
  // Returns true when a staged real original Xbox model became GPU-resident.
  bool TickSourceVerifiedModels();
  // Production source-only publication is transactional: if adoption throws,
  // keep the exact uploaded GPU model for retry before consuming another
  // staged asset. The callback must be idempotent for partial adoption.
  template<class Publish>
  bool TickAndPublishSourceVerifiedModel(Publish&& publish) {
    if (!LastUploadedOriginalSourceModel() && !TickSourceVerifiedModels())
      return false;
    auto resident=LastUploadedOriginalSourceModel();
    if (!resident)
      throw std::logic_error("Original source upload lost before publication");
    publish(resident);
    last_uploaded_original_model_hash_=0;
    return true;
  }
  // A bounded batch avoids starving a newly visible city block behind
  // a one-model-per-Present upload cap. I/O remains asynchronous. An
  // individual GPU upload may exceed the soft time budget; do not start
  // another after that, and never lose the pending publication on failure.
  template<class Publish>
  size_t TickAndPublishSourceVerifiedModels(size_t max_count,
      std::chrono::steady_clock::duration budget,Publish&& publish) {
    if(lod_)throw std::logic_error("Source-only batch requires no inferred LOD tiers");
    if(!max_count || budget<=std::chrono::steady_clock::duration::zero())return 0;
    const auto start=std::chrono::steady_clock::now();
    size_t count=0;
    while(count<max_count) {
      if(count && std::chrono::steady_clock::now()-start>=budget)break;
      if(!TickAndPublishSourceVerifiedModel(publish))break;
      ++count;
    }
    return count;
  }
  // The last original model GPU upload is owned by THIS persistent
  // stream and uses exactly the same Diligent Vulkan device as the
  // active direct renderer. Only access on the render thread directly
  // after TickSourceVerifiedModels() returned true.
  std::shared_ptr<const model::UploadedOriginalModel>
  LastUploadedOriginalSourceModel() const noexcept;
  // The game world can reference many real EoR models per frame, not just
  // the three camera-distance LOD tiers. Supply hashes identified by the
  // independently authenticated CURRENT XDK Draw->MODELS.ARC witness.
  // Request only stages source-original data; renderer eligibility remains
  // gated by surface/material/VS/PS provenance on that exact Draw.
  // Call only on the render thread; one async model load/upload per Tick
  // preserves a bounded frame upload budget.
  void RequestSourceVerifiedModels(std::span<const uint32_t> model_hashes,
                                   bool current_draw=false);
  const model::UploadedOriginalModel* Resident(uint32_t original_hash) const noexcept;
  // Strong original-source GPU ownership for an independently verified
  // EoR Draw frame. The caller keeps this reference until the Vulkan
  // submission's GPU completion fence. Unlike a borrowed raw pointer, it
  // survives world streaming reconfiguration or the model map clearing.
  std::shared_ptr<const model::UploadedOriginalModel> ResidentShared(
      uint32_t original_hash) const noexcept;
  bool Failed(uint32_t original_hash) const;
  bool UsesGpuBackend(const model::NativeMeshUploadBackend& gpu) const noexcept {
    return &backend_==&gpu;
  }
  void RetryFailed(uint32_t original_hash);
 private:
  void Queue(uint32_t hash, bool urgent);
  void Worker();
  model::NativeMeshUploadBackend& backend_; // outlives this object
  std::optional<ResidentLodSet> lod_;
  std::map<uint32_t,std::shared_ptr<model::UploadedOriginalModel>> gpu_; // GPU buffers shared with submitted frames
  uint32_t last_uploaded_original_model_hash_=0; // render thread only
  std::unique_ptr<ModelSource> source_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<uint32_t> requests_;
  std::set<uint32_t> queued_;
  std::set<uint32_t> failed_;
  // Protected by mutex_. Never drop a source-proven world model simply
  // because it is not the Hulk-only active/prefetch LOD tier.
  std::set<uint32_t> source_requested_;
  // Current-frame demands may overtake speculative archive candidates
  // already queued or staged. Never change model identities or LOD tiers.
  std::set<uint32_t> source_urgent_;
  // Persistent provenance: a failed world-model load remains retryable even
  // after it is removed from the active request window.
  std::set<uint32_t> source_known_;
  // Source-only city streaming can read ahead while the render thread
  // prepares the current frame. The camera-driven tier path remains a
  // single slot so speculative tiers cannot delay an urgent near LOD.
  std::deque<std::pair<uint32_t,model::LoadedNativeGeometry>> staged_;
  static constexpr size_t kSourceReadAhead=4;
  bool stopping_ = false;
  std::thread worker_;
};
}  // namespace hulk::eor::world
