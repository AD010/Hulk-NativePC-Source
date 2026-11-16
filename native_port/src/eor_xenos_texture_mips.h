#pragma once
// CPU-only original Xbox 360 Xenos 2D block untile / endian / mip converter.
// Requires EXPLICIT original image format, pitch, tiling, endian and per-mip
// source offsets. The original LFXT header parser/Draw sampler ABI must provide
// these values; no metadata is fabricated from the archive's byte length.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hulk::eor::textures {
enum class XenosTextureFormat : uint8_t {
  Unknown=0, BC1, BC2, BC3, RGBA8
};
enum class XenosDataEndian : uint8_t {
  Unknown=0, NoSwap, Swap16, Swap32
};
enum class XenosTileMode : uint8_t {
  Unknown=0, Linear, Tiled2D
};
struct SourceMipLayout {
  bool from_stream_archive=false;
  uint64_t source_offset=0;
  uint32_t width=0, height=0; // actual mip image, texels
  uint32_t storage_pitch_blocks=0, storage_height_blocks=0;
  uint32_t source_x_blocks=0, source_y_blocks=0; // packed-tail position
  XenosTileMode tiling=XenosTileMode::Unknown;
  XenosDataEndian endian=XenosDataEndian::Unknown;
};
struct LinearHostMip {
  uint32_t width=0,height=0,row_pitch_bytes=0;
  std::vector<std::byte> blocks; // BCn block payload or RGBA8 bytes
};
struct PreparedHostImage {
  XenosTextureFormat format=XenosTextureFormat::Unknown;
  std::vector<LinearHostMip> mips; // level 0..N without gaps
};
uint64_t OriginalTiled2DOffset(uint32_t x_block,uint32_t y_block,
                               uint32_t aligned_pitch_blocks,
                               uint32_t bytes_per_block);
PreparedHostImage PrepareExplicitOriginalMips(
    XenosTextureFormat format,
    std::span<const SourceMipLayout> source_layout,
    std::span<const std::byte> original_texture_payload,
    std::span<const std::byte> original_stream_payload);
} // namespace hulk::eor::textures
