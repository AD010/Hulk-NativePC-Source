#include "eor_native_world_stream.h"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace hulk::eor::world {
NativeWorldStream::NativeWorldStream(std::unique_ptr<ModelSource> source,
    model::NativeMeshUploadBackend& backend, std::vector<LodTier> tiers,
    LodPolicy policy)
    : backend_(backend), lod_(std::move(tiers), policy), source_(std::move(source)) {
  if (!source_) throw std::invalid_argument("No verified original EoR model archive source");
  worker_ = std::thread([this] { Worker(); });
}
NativeWorldStream::~NativeWorldStream() noexcept {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    requests_.clear();
  }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
  // Caller must first complete GPU fences; we do not evict in-flight models.
  gpu_.clear();
}
const model::UploadedOriginalModel* NativeWorldStream::Resident(uint32_t hash) const noexcept {
  const auto it = gpu_.find(hash);
  return it == gpu_.end() ? nullptr : &it->second;
}
bool NativeWorldStream::Failed(uint32_t hash) const {
  std::lock_guard lock(mutex_);
  return failed_.contains(hash);
}
void NativeWorldStream::RetryFailed(uint32_t hash) {
  // Only known original LOD identifiers may be retried.
  bool known=false;
  for (const auto& tier:lod_.Tiers()) if (tier.original_model_hash==hash) known=true;
  if(!known) throw std::invalid_argument("Unrecognized original EoR LOD resource hash");
  std::lock_guard lock(mutex_);
  failed_.erase(hash);
}
void NativeWorldStream::Queue(uint32_t hash) {
  if (!hash || gpu_.contains(hash)) return;
  std::lock_guard lock(mutex_);
  if (failed_.contains(hash) || queued_.contains(hash) || stopping_) return;
  queued_.insert(hash);
  requests_.push_back(hash);
  wake_.notify_one();
}
LodDecision NativeWorldStream::Tick(double distance) {
  // Invalid camera data must not consume a prepared asset or queue new work.
  if (!(distance >= 0.0) || !std::isfinite(distance))
    throw std::invalid_argument("Invalid native world-stream camera distance");
  std::optional<std::pair<uint32_t, model::LoadedNativeGeometry>> prepared;
  {
    std::lock_guard lock(mutex_);
    if (staged_) prepared.swap(staged_);
  }
  if (prepared) {
    const uint32_t hash=prepared->first;
    try {
      if (prepared->second.model.original_resource_hash != hash)
        throw std::invalid_argument("Original model index/hash changed during streaming");
      auto uploaded=model::UploadOriginalModel(std::move(prepared->second),backend_);
      if (!gpu_.emplace(hash,std::move(uploaded)).second)
        throw std::logic_error("Original EoR GPU model already resident");
      lod_.SetResident(hash,true);
    } catch (...) {
      std::lock_guard lock(mutex_);
      failed_.insert(hash);
    }
    {
      std::lock_guard lock(mutex_);
      queued_.erase(hash);
    }
    wake_.notify_one();
  }
  auto decision=lod_.Update(distance);
  Queue(decision.request_model_hash);
  Queue(decision.prefetch_model_hash);
  return decision;
}
void NativeWorldStream::Worker() {
  for (;;) {
    uint32_t hash=0;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock,[this]{return stopping_ || (!requests_.empty() && !staged_);});
      if (stopping_) return;
      hash=requests_.front();requests_.pop_front();
    }
    std::optional<model::LoadedNativeGeometry> raw;
    try { raw=source_->Load(hash); } catch (...) { raw.reset(); }
    {
      std::lock_guard lock(mutex_);
      if (stopping_) return;
      if (raw) staged_.emplace(hash,std::move(*raw));
      else { failed_.insert(hash);queued_.erase(hash); }
    }
    wake_.notify_all();
  }
}
}  // namespace hulk::eor::world
