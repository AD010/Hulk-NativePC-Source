#pragma once
// Original Hulk EoR model -> texture resource ID candidates (no guest runtime).
// Correlation with the indexed resource is NOT proof of a live sampler binding.
#include "eor_archive.h"
#include "eor_model_resource.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace hulk::eor::model {
struct TextureReferenceCandidate {
  uint32_t original_texture_hash = 0;
  uint32_t offset_from_surface_data = 0;
  Resource original_resource{};
};
std::vector<TextureReferenceCandidate> FirstSurfaceTextureCandidates(
    std::span<const std::byte> original_model,
    const Index& matching_platform_index);
}  // namespace hulk::eor::model
