#pragma once
// Native EoR world-streaming LOD residency policy. All variant hashes and
// distance boundaries MUST come from a verified game world/LOD source.
// This does not discover missing tiers or rewrite the Xbox 360's LOD logic.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hulk::eor::world {
struct LodTier {
  uint32_t original_model_hash = 0;
  // Furthest camera distance in original world units for this tier.
  // Strictly ascending; the last tier must have +infinity.
  double maximum_distance = 0.0;
};
struct LodPolicy {
  // >1 postpones transitions to less detailed tiers without changing camera
  // positions, engine culling, world dimensions, or texture MIP selection.
  double distance_multiplier = 1.0;
  double hysteresis = 0.12;
  double prefetch_fraction = 0.25;
};
struct LodDecision {
  uint32_t active_model_hash = 0;   // 0 means no resident model is available
  uint32_t request_model_hash = 0;  // mandatory next tier, nonresident
  uint32_t prefetch_model_hash = 0; // adjacent tier; avoid I/O on render thread
  size_t active_tier = 0;
  size_t desired_tier = 0;
  bool pending_upgrade = false;
};

class ResidentLodSet {
 public:
  // Fail closed on unverified, unordered or duplicate source metadata.
  explicit ResidentLodSet(std::vector<LodTier> verified_tiers,
                          LodPolicy policy = {});
  void SetResident(uint32_t original_model_hash, bool resident);
  LodDecision Update(double camera_distance_world_units);
  void Reset() noexcept { active_.reset(); }
  std::optional<uint32_t> ActiveModel() const noexcept;
  std::span<const LodTier> Tiers() const noexcept { return tiers_; }
 private:
  std::vector<LodTier> tiers_;
  std::vector<bool> resident_;
  LodPolicy policy_;
  std::optional<size_t> active_;
};
} // namespace hulk::eor::world
