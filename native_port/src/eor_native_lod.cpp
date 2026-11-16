#include "eor_native_lod.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hulk::eor::world {
ResidentLodSet::ResidentLodSet(std::vector<LodTier> tiers, LodPolicy policy)
    : tiers_(std::move(tiers)), resident_(tiers_.size(), false), policy_(policy) {
  if (tiers_.empty() || tiers_.size() > 32 ||
      !std::isfinite(policy.distance_multiplier) ||
      policy.distance_multiplier < 1.0 || policy.distance_multiplier > 16.0 ||
      !std::isfinite(policy.hysteresis) || policy.hysteresis < 0.0 ||
      policy.hysteresis > 0.4 ||
      !std::isfinite(policy.prefetch_fraction) ||
      policy.prefetch_fraction < 0.0 || policy.prefetch_fraction > 0.9)
    throw std::invalid_argument("Invalid original EoR LOD residency policy");
  double previous = 0.0;
  for (size_t i = 0; i < tiers_.size(); ++i) {
    const auto& tier = tiers_[i];
    if (!tier.original_model_hash ||
        tier.maximum_distance <= previous ||
        ((i + 1 == tiers_.size()) != std::isinf(tier.maximum_distance)) ||
        (std::isinf(tier.maximum_distance) && tier.maximum_distance < 0))
      throw std::invalid_argument("Invalid original EoR LOD tier boundaries");
    for (size_t j = 0; j < i; ++j)
      if (tier.original_model_hash == tiers_[j].original_model_hash)
        throw std::invalid_argument("Duplicate original EoR LOD model hash");
    previous = tier.maximum_distance;
  }
}

void ResidentLodSet::SetResident(uint32_t hash, bool resident) {
  for (size_t i = 0; i < tiers_.size(); ++i) {
    if (tiers_[i].original_model_hash != hash) continue;
    resident_[i] = resident;
    if (!resident && active_ == i) active_.reset();
    return;
  }
  throw std::invalid_argument("Unknown EoR LOD resource hash");
}

std::optional<uint32_t> ResidentLodSet::ActiveModel() const noexcept {
  if (!active_ || !resident_[*active_]) return std::nullopt;
  return tiers_[*active_].original_model_hash;
}

LodDecision ResidentLodSet::Update(double distance) {
  if (!std::isfinite(distance) || distance < 0.0)
    throw std::invalid_argument("Invalid EoR LOD camera distance");
  const double d = distance / policy_.distance_multiplier;
  size_t target = 0;
  while (target + 1 < tiers_.size() && d > tiers_[target].maximum_distance)
    ++target;
  // Only a resident active model may delay the next switch. This avoids
  // thrashing from camera jitter on both sides of an original threshold.
  if (active_ && resident_[*active_]) {
    if (target > *active_ &&
        d <= tiers_[*active_].maximum_distance * (1.0 + policy_.hysteresis))
      target = *active_;
    else if (target < *active_ &&
             d >= tiers_[target].maximum_distance * (1.0 - policy_.hysteresis))
      target = *active_;
  }
  LodDecision result;
  result.desired_tier = target;
  if (resident_[target]) {
    active_ = target;
  } else {
    result.request_model_hash = tiers_[target].original_model_hash;
    // Hold old detail while its replacement streams in. If nothing has been
    // shown yet, fall back to the nearest resident tier, preferring a less
    // detailed variant in ties rather than submitting an invalid draw.
    if (!active_ || !resident_[*active_]) {
      active_.reset();
      for (size_t gap = 1; gap < tiers_.size() && !active_; ++gap) {
        if (target + gap < tiers_.size() && resident_[target + gap])
          active_ = target + gap;
        else if (gap <= target && resident_[target - gap])
          active_ = target - gap;
      }
    }
  }
  result.pending_upgrade = result.request_model_hash != 0;
  if (!active_) return result;
  result.active_tier = *active_;
  result.active_model_hash = tiers_[*active_].original_model_hash;
  // Request adjacent LODs before crossing the boundary. The renderer never
  // loads bytes synchronously and never retires the current buffer here.
  const auto propose = [&](size_t tier) {
    if (!resident_[tier] &&
        tiers_[tier].original_model_hash != result.request_model_hash)
      result.prefetch_model_hash = tiers_[tier].original_model_hash;
  };
  if (*active_ + 1 < tiers_.size() &&
      d >= tiers_[*active_].maximum_distance * (1.0 - policy_.prefetch_fraction))
    propose(*active_ + 1);
  if (!result.prefetch_model_hash && *active_ > 0 &&
      d <= tiers_[*active_ - 1].maximum_distance * (1.0 + policy_.prefetch_fraction))
    propose(*active_ - 1);
  return result;
}
} // namespace hulk::eor::world
