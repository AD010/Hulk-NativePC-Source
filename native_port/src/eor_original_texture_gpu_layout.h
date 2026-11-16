#pragma once
// Source-grounded original USA EoR Xbox texture fetch metadata.
// Decoded from 52-byte *trailing* descriptors found in both original
// Textures.ARC/LFXT and TexStreams.ARC records, not guessed from name.
#include "eor_original_texture_streams.h"
#include "eor_xenos_texture_mips.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace hulk::eor::textures {
struct VerifiedOriginalXboxGpuMipChain {
  XenosTextureFormat format=XenosTextureFormat::Unknown;
  std::vector<SourceMipLayout> layouts; // largest -> smallest; real file offsets
};
// Supports observed Xenos format 0x12 (DXT1), 0x14 (DXT4/5) and
// 0x1D (RGBA16_EXPAND / 16-bit float upload family).
// Requires EXACT matching original full record extents, 52-byte trailing
// descriptor structures and format/dimensions at both endpoints.
// A logical mip allocation cannot prove color space or guest Draw owner.
std::optional<VerifiedOriginalXboxGpuMipChain> DecodeOriginalXboxGpuMips(
    std::span<const std::byte> original_lfxt,
    std::span<const std::byte> original_texstream,
    const OriginalLfxtInlineSection& inline_section,
    const OriginalLfxtStreamTable& stream_table,
    const OriginalLfxtMipPlan& mip_plan);
} // namespace hulk::eor::textures
