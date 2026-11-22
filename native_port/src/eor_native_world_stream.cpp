#include "eor_native_world_stream.h"
#include <algorithm>
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
  return it == gpu_.end() ? nullptr : it->second.get();
}
std::shared_ptr<const model::UploadedOriginalModel>
NativeWorldStream::ResidentShared(uint32_t hash) const noexcept {
  const auto it = gpu_.find(hash);
  return it == gpu_.end() ? nullptr : it->second;
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
void NativeWorldStream::Queue(uint32_t hash, bool urgent) {
  if (!hash || gpu_.contains(hash)) return;
  std::lock_guard lock(mutex_);
  if (failed_.contains(hash) || stopping_) return;
  if (queued_.contains(hash)) {
    // A model queued as distant prefetch can become the camera's needed
    // high-detail tier before the worker reaches it: promote it immediately.
    if (urgent) {
      const auto it = std::find(requests_.begin(), requests_.end(), hash);
      if (it != requests_.end()) {
        // Reorder in place without allocating: erase+push_front could
        // throw mid-promotion and permanently strand a queued model.
        std::rotate(requests_.begin(), it, it + 1);
      }
    }
    return;
  }
  queued_.insert(hash);
  try {
    if (urgent) requests_.push_front(hash);
    else requests_.push_back(hash);
  } catch (...) {
    queued_.erase(hash);
    throw;
  }
  wake_.notify_one();
}
void NativeWorldStream::RequestSourceVerifiedModels(
    std::span<const uint32_t> hashes) {
  // The authenticated USA source archive enumerates 4,174 models; the
  // request window must support that full game catalogue, including
  // streamed city buildings, not merely the player LOD set.
  constexpr size_t kOriginalWorldModelLimit=16384;
  if(hashes.size()>kOriginalWorldModelLimit)
    throw std::invalid_argument("Too many original world model source owners");
  for(const uint32_t hash:hashes)
    if(!hash)throw std::invalid_argument("Null original world model hash");
  // No GPU calls, guest buffer reads or archive material guesses here:
  // this only queues original-model I/O. A complete shader/material
  // witness is still required by the native Vulkan Draw resolver.
  for(const uint32_t hash:hashes) {
    if(gpu_.contains(hash))continue;
    {
      std::lock_guard lock(mutex_);
      if(stopping_)return;
      if(failed_.contains(hash))continue; // RetryFailed first
      if(source_requested_.size()>=kOriginalWorldModelLimit &&
         !source_requested_.contains(hash))
        throw std::invalid_argument("Original streamed model request cap reached");
      source_requested_.insert(hash);
    }
    Queue(hash,false); // urgent LOD stays ahead of background world models
  }
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
  // Re-evaluate current camera demand BEFORE touching GPU memory. A fast
  // camera jump can make an in-flight read irrelevant; don't upload a stale
  // building model into VRAM, or let prefetch starve the needed near LOD.
  const LodDecision needed = lod_.Update(distance);
  if (prepared) {
    const uint32_t hash = prepared->first;
    bool source_required=false;
    {
      std::lock_guard lock(mutex_);
      source_required=source_requested_.contains(hash);
    }
    const bool useful = source_required ||
                        hash == needed.request_model_hash ||
                        hash == needed.prefetch_model_hash ||
                        hash == needed.active_model_hash;
    if (useful) {
      try {
        if (prepared->second.model.original_resource_hash != hash)
          throw std::invalid_argument("Original model index/hash changed during streaming");
        auto uploaded=std::make_shared<model::UploadedOriginalModel>(
            model::UploadOriginalModel(std::move(prepared->second),backend_));
        if (!gpu_.emplace(hash,std::move(uploaded)).second)
          throw std::logic_error("Original EoR GPU model already resident");
        // World buildings, civilians, vehicles and UI models are NOT
        // necessarily player LOD-tier entries. ResidentLodSet explicitly
        // rejects unknown hashes: a valid original GPU upload must never
        // be mislabeled as a failed stream merely for not being a Hulk LOD.
        for(const auto& tier:lod_.Tiers())
          if(tier.original_model_hash==hash) {
            lod_.SetResident(hash,true);
            break;
          }
      } catch (...) {
        std::lock_guard lock(mutex_);
        failed_.insert(hash);
        source_requested_.erase(hash);
      }
    }
    {
      std::lock_guard lock(mutex_);
      queued_.erase(hash);
      // The immutable uploaded model now owns its buffers. Source hash
      // remains in the full EoR material catalogue, not in this queue.
      if(gpu_.contains(hash))source_requested_.erase(hash);
    }
    wake_.notify_one();
  }
  auto decision = lod_.Update(distance);
  Queue(decision.request_model_hash, true);  // needed by this frame
  Queue(decision.prefetch_model_hash, false); // can wait behind urgent loads
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
      else {
        failed_.insert(hash);
        queued_.erase(hash);
        // An absent/corrupt original asset cannot permanently consume
        // a slot in the authentic world-model source request window.
        source_requested_.erase(hash);
      }
    }
    wake_.notify_all();
  }
}
}  // namespace hulk::eor::world
