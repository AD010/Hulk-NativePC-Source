#pragma once
// Original Xbox 360 Hulk EoR LFXT -> TexStreams archive mapping.
// Legacy Xbox data audit: 5836 links (5820 at +34, 16 at +22).
// This count is NOT verified against the separately pinned USA INDEX.IND.
// A verified stream reference is NOT a decoded image/mip layout or sampler.
#include "eor_archive.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>
namespace hulk::eor::textures {
struct OriginalStreamLink {
  uint32_t original_texture_hash=0;
  uint32_t original_stream_hash=0;
  Resource indexed_stream{};
  size_t original_stream_hash_offset=0;
};
std::optional<OriginalStreamLink> FindVerifiedOriginalTexStream(
    std::span<const std::byte> original_texture,
    uint32_t original_texture_hash, const Index& original_xbox_index);

// TexStreams records observed in ten original Xbox extracts start with a
// big-endian 32-bit size of the *remaining* record bytes. The four-byte
// envelope itself is excluded from this size. The remainder is opaque:
// do NOT drop an additional 52 bytes or assume any GPU format/mip layout.
// The returned span points into the caller-owned original byte buffer.
std::span<const std::byte> ValidateOriginalTexStreamEnvelope(
    std::span<const std::byte> complete_indexed_record);
std::vector<std::byte> ReadOriginalTexStream(
    const std::filesystem::path& original_stream_archive,
    const OriginalStreamLink& proven_link);
}
