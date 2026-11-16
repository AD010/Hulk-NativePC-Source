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
// Exact nested remaining-length fields found in all ten genuine USA LFXT
// descriptors. Their location varies with header length and texture type.
// This establishes a BINARY SECTION BOUNDARY, not the GPU format/mip layout.
struct OriginalLfxtInlineSection {
  size_t outer_length_field_offset=0;
  size_t inner_length_field_offset=0;
  size_t inline_data_offset=0;
  size_t inline_data_bytes=0;
};
// Returns nullopt for unsupported layouts; NEVER assumes a fixed 0x38 or
// guessed position after the original LFXT name. Validates real name/hash.
// Real USA LFXT structure: four bytes STREAM hash, 4 unknown bytes,
 // one-byte level count, then count * (big-endian aligned byte size,
 // big-endian IEEE754 positive level metric). Supported layout is only
 // accepted if all level sizes match the corresponding genuine indexed
 // TexStreams record length minus the observed 56-byte record overhead.
struct OriginalLfxtStreamLevel {
  uint32_t source_aligned_bytes=0;
  float original_level_metric=0.0f;
};
struct OriginalLfxtStreamTable {
  size_t original_count_offset=0;
  size_t original_entries_offset=0;
  std::vector<OriginalLfxtStreamLevel> levels;
  uint64_t summed_source_bytes=0;
};
std::optional<OriginalLfxtStreamTable> ParseOriginalLfxtStreamTable(
    std::span<const std::byte> complete_original_lfxt,
    const OriginalStreamLink& indexed_original_stream);

std::optional<OriginalLfxtInlineSection> FindOriginalLfxtInlineSection(
    std::span<const std::byte> complete_original_lfxt,
    uint32_t original_texture_hash);

std::span<const std::byte> ValidateOriginalTexStreamEnvelope(
    std::span<const std::byte> complete_indexed_record);
// Offsets in explicitly verified GPU mip layouts are relative to the
// COMPLETE original indexed record, INCLUDING its BE u32 length field.
// No legal texel block may read that four-byte envelope.
std::span<const std::byte> VerifiedOriginalStreamByteRange(
    std::span<const std::byte> complete_indexed_record,
    uint64_t original_record_offset, uint64_t requested_byte_count);
std::vector<std::byte> ReadOriginalTexStream(
    const std::filesystem::path& original_stream_archive,
    const OriginalStreamLink& proven_link);
}
