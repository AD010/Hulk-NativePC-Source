#pragma once
// True original-EoR model I/O worker -> host-GPU upload on caller/render thread.
// Never issues Xenos commands, invents a surface-to-mesh mapping or replaces
// a still-in-flight GPU buffer. Client owns verified game distance/LOD tiers.
#include "eor_original_model_upload.h"
#include "eor_native_lod.h"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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
  const model::UploadedOriginalModel* Resident(uint32_t original_hash) const noexcept;
  bool Failed(uint32_t original_hash) const;
  void RetryFailed(uint32_t original_hash);
 private:
  void Queue(uint32_t hash);
  void Worker();
  model::NativeMeshUploadBackend& backend_; // outlives this object
  ResidentLodSet lod_;
  std::map<uint32_t, model::UploadedOriginalModel> gpu_; // never auto-evict in-flight buffers
  std::unique_ptr<ModelSource> source_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<uint32_t> requests_;
  std::set<uint32_t> queued_;
  std::set<uint32_t> failed_;
  std::optional<std::pair<uint32_t,model::LoadedNativeGeometry>> staged_;
  bool stopping_ = false;
  std::thread worker_;
};
}  // namespace hulk::eor::world
