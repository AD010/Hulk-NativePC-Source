#include "eor_original_texture_mip_ranges.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace hulk::eor::textures {
namespace {
struct BlockInfo {
  OriginalLfxtStorageFamily family;
  uint32_t pixels_per_block=0, bytes_per_block=0;
};
BlockInfo Info(XenosTextureFormat f) {
  switch(f) {
    case XenosTextureFormat::BC1:
    case XenosTextureFormat::BC4:
      return {OriginalLfxtStorageFamily::BC_8ByteBlock,4,8};
    case XenosTextureFormat::BC2:
    case XenosTextureFormat::BC3:
    case XenosTextureFormat::BC5:
      return {OriginalLfxtStorageFamily::BC_16ByteBlock,4,16};
    case XenosTextureFormat::RGBA16F:
      return {OriginalLfxtStorageFamily::Texel_8Byte,1,8};
    default:
      throw std::invalid_argument("Unknown original EoR format family; cannot verify mip ranges");
  }
}
uint64_t Round32(uint64_t n) {return (n+31u)&~uint64_t(31);}
}
void ValidateOriginalMipPayloadRanges(
    std::span<const std::byte> lfxt,
    std::span<const std::byte> stream,
    const OriginalLfxtInlineSection& section,
    const OriginalLfxtMipPlan& plan,
    XenosTextureFormat format,
    std::span<const SourceMipLayout> layouts) {
  const auto info=Info(format);
  if(plan.family!=info.family || plan.levels.empty() ||
     plan.levels.size()!=layouts.size() || layouts.size()>14)
    throw std::invalid_argument("Original USA LFXT mip plan missing/format-family mismatch");
  (void)ValidateOriginalTexStreamEnvelope(stream);
  if(stream.size()<56 || lfxt.size()<52 ||
     section.inline_data_offset>lfxt.size()-52 ||
     section.inline_data_bytes!=lfxt.size()-section.inline_data_offset ||
     section.inline_data_bytes<52)
    throw std::invalid_argument("Incomplete source TexStreams/LFXT mip envelope");
  // All 10 original samples contain 52 excess bytes after the documented
  // aligned mip allocations. Do NOT treat these bytes as texels. Their
  // semantic meaning remains unknown; layouts stay explicit.
  const uint64_t stream_end=stream.size()-52;
  const uint64_t inline_end=lfxt.size()-52;
  uint64_t stream_pos=4;
  uint64_t inline_pos=section.inline_data_offset;
  bool seen_inline=false;
  for(size_t i=0;i<layouts.size();++i) {
    const auto& l=layouts[i];
    const auto& original=plan.levels[i];
    if(!original.aligned_storage_bytes ||
       !original.width || !original.height ||
       l.width!=original.width || l.height!=original.height ||
       l.from_stream_archive!=original.from_stream_archive ||
       l.tiling==XenosTileMode::Unknown ||
       l.endian==XenosDataEndian::Unknown)
      throw std::invalid_argument("Unverified original Xbox mip identity/layout");
    const uint64_t block_w=(uint64_t(l.width)+info.pixels_per_block-1)/
                           info.pixels_per_block;
    const uint64_t block_h=(uint64_t(l.height)+info.pixels_per_block-1)/
                           info.pixels_per_block;
    if(l.storage_pitch_blocks!=Round32(block_w) ||
       l.storage_height_blocks!=Round32(block_h) ||
       l.storage_pitch_blocks==0 || l.storage_height_blocks==0 ||
       l.source_x_blocks || l.source_y_blocks ||
       uint64_t(l.storage_pitch_blocks)*l.storage_height_blocks*
               info.bytes_per_block!=original.aligned_storage_bytes)
      throw std::invalid_argument("Xenos mip storage pitch is not the original 32x32 allocation");
    if(original.from_stream_archive) {
      if(seen_inline || l.source_offset!=stream_pos ||
         uint64_t(original.aligned_storage_bytes)>stream_end-stream_pos)
        throw std::invalid_argument("Original Xenos streamed mip offsets not contiguous");
      stream_pos+=original.aligned_storage_bytes;
    } else {
      seen_inline=true;
      if(l.source_offset!=inline_pos ||
         uint64_t(original.aligned_storage_bytes)>inline_end-inline_pos)
        throw std::invalid_argument("Original LFXT resident mip offsets not contiguous");
      inline_pos+=original.aligned_storage_bytes;
    }
  }
  if(stream_pos!=stream_end || inline_pos!=inline_end)
    throw std::invalid_argument("Original USA mip allocations do not consume real data extents");
}
} // namespace hulk::eor::textures
