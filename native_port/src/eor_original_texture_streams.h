#pragma once
// Original Xbox 360 Hulk EoR LFXT -> TexStreams archive mapping.
// Private source audit: 5836 indexed stream links, 5820 at +34, 16 at +22.
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
std::vector<std::byte> ReadOriginalTexStream(
    const std::filesystem::path& original_stream_archive,
    const OriginalStreamLink& proven_link);
}
