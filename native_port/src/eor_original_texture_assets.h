#pragma once
// Immutable, source-faithful Xbox 360 EoR Textures.IND/ARC resource loader.
// Original Xenos texture payloads remain opaque: hashes found beside a model
// surface are CANDIDATES, not established sampler bindings or GPU formats.
#include "eor_archive.h"
#include "eor_original_texture_streams.h"
#include "eor_original_texture_gpu_layout.h"
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
  std::optional<OriginalStreamLink> original_stream;
  std::vector<std::byte> original_stream_bytes; // optional original TexStreams ARC payload
  // Set only by OpenPinnedUsa after SHA-256 verification, never by generic Open.
  bool pinned_usa_index = false;
  // Optional authentic suffix boundary, no inferred GPU layout or mip size.
  std::optional<OriginalLfxtInlineSection> original_inline_section;
  // Structural exact size-table proof, not format/tiling/mip-address proof.
  std::optional<OriginalLfxtStreamTable> original_stream_table;
  // Verified mip dimensions/aligned allocations from authentic USA LFXT;
  // still not a decoded GPU format nor physical pixel source offset.
  std::optional<OriginalLfxtMipPlan> original_mip_plan;
  // Concrete source-proven Xenos format, byte starts and full mip layouts.
  std::optional<VerifiedOriginalXboxGpuMipChain> verified_gpu_mips;
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
                                        const std::filesystem::path& xbox_textures_arc,
                                        const std::filesystem::path& xbox_texstreams_arc = {});
  // Unlike generic Open (for offline audit and synthetic fixtures), this
  // entrypoint checks the pinned original USA INDEX.IND SHA-256 before I/O.
  // A European Xbox index is never substituted based on similar structure.
  static OriginalXboxTextureAssets OpenPinnedUsa(
      const std::filesystem::path& xbox_usa_index,
      const std::filesystem::path& xbox_usa_textures_arc,
      const std::filesystem::path& xbox_usa_texstreams_arc = {});
  std::optional<OriginalTexture> LoadHash(uint32_t original_hash) const;
  // Preflights every candidate against the original Xbox index BEFORE doing
  // disk I/O. Duplicate hash references retain their original slot positions
  // but the corresponding opaque texture bytes are read only once.
  OriginalTextureBatch LoadSurfaceCandidates(
      std::span<const model::SurfaceMaterials> surfaces) const;
 private:
  Index index_;
  std::filesystem::path archive_;
  std::filesystem::path stream_archive_;
  bool pinned_usa_index_ = false;
};
} // namespace hulk::eor::textures
