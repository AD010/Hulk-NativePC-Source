#pragma once
// Canonical physical storage geometry. Equal block size does not imply
// equal GPU channel or sampler semantics (e.g. BC1 and BC4).
#include "eor_xenos_texture_mips.h"
#include "eor_original_texture_streams.h"
#include <stdexcept>

namespace hulk::eor::textures {
struct TextureBlockInfo {
  OriginalLfxtStorageFamily family;
  uint32_t pixels_per_block,bytes_per_block;
};
inline constexpr TextureBlockInfo GetTextureBlockInfo(XenosTextureFormat format) {
  using F=XenosTextureFormat;
  using S=OriginalLfxtStorageFamily;
  switch(format) {
    case F::BC1: case F::BC4: return {S::BC_8ByteBlock,4,8};
    case F::BC2: case F::BC3: case F::BC5: return {S::BC_16ByteBlock,4,16};
    case F::RGBA8: return {S::Texel_4Byte,1,4};
    case F::RGBA16F: return {S::Texel_8Byte,1,8};
    case F::R8: return {S::Texel_1Byte,1,1};
    default: throw std::invalid_argument("Unknown original Xenos texture format");
  }
}
inline constexpr bool ValidTextureTileMode(XenosTileMode mode) noexcept {
  return mode==XenosTileMode::Linear || mode==XenosTileMode::Tiled2D;
}
inline constexpr bool ValidTextureEndian(XenosDataEndian endian) noexcept {
  return endian==XenosDataEndian::NoSwap || endian==XenosDataEndian::Swap16 ||
         endian==XenosDataEndian::Swap32;
}
} // namespace hulk::eor::textures
