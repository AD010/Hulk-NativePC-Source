#pragma once
// Immutable, source-faithful Xbox 360 EoR Textures.IND/ARC resource loader.
// Original Xenos texture payloads remain opaque: hashes found beside a model
// surface are CANDIDATES, not established sampler bindings or GPU formats.
#include "eor_archive.h"
#include "eor_model_surface_materials.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace hulk::eor::textures {
struct OriginalTexture {
  uint32_t hash = 0;
  Resource source_record{};
  std::vector<std::byte> original_bytes;
};
struct CandidateSlot {
  uint32_t offset_from_surface_data = 0;
  uint32_t texture_hash = 0;
};
struct SurfaceCandidates {
  uint32_t ordinal = 0;
  std::string original_material_name;
  std::vector<CandidateSlot> candidate_slots;
};
struct OriginalTextureBatch {
  std::vector<SurfaceCandidates> surfaces;
  std::vector<OriginalTexture> unique_textures;
};
class OriginalXboxTextureAssets {
 public:
  static OriginalXboxTextureAssets Open(const std::filesystem::path& xbox_index,
                                        const std::filesystem::path& xbox_textures_arc);
  std::optional<OriginalTexture> LoadHash(uint32_t original_hash) const;
  // Preflights every candidate against the original Xbox index BEFORE doing
  // disk I/O. Duplicate hash references retain their original slot positions
  // but the corresponding opaque texture bytes are read only once.
  OriginalTextureBatch LoadSurfaceCandidates(
      std::span<const model::SurfaceMaterials> surfaces) const;
 private:
  Index index_;
  std::filesystem::path archive_;
};
} // namespace hulk::eor::textures
