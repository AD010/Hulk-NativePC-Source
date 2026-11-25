#include "eor_xenos_texture_mips.h"
#include "eor_texture_format_info.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
namespace hulk::eor::textures {
namespace {
uint32_t CeilDiv(uint32_t n,uint32_t div) {return (n-1)/div+1;}
void ConvertEndian(std::byte* output,const std::byte* input,
                   uint32_t count,XenosDataEndian e) {
  if(e==XenosDataEndian::NoSwap) {
    std::copy_n(input,count,output);return;
  }
  if(e==XenosDataEndian::Swap16) {
    if(count%2) throw std::invalid_argument("Xenos endian/byte count mismatch");
    for(uint32_t i=0;i<count;i+=2){output[i]=input[i+1];output[i+1]=input[i];}
    return;
  }
  if(e==XenosDataEndian::Swap32) {
    if(count%4) throw std::invalid_argument("Xenos endian/byte count mismatch");
    for(uint32_t i=0;i<count;i+=4)for(uint32_t j=0;j<4;++j)
      output[i+j]=input[i+3-j];
    return;
  }
  throw std::invalid_argument("Unknown original Xenos texture fetch endian");
}
}
uint64_t OriginalTiled2DOffset(
    uint32_t x,uint32_t y,uint32_t pitch,uint32_t bpb) {
  if(!pitch || pitch>8192 || pitch%32 ||
     x>=pitch || y>=8192 ||
     (bpb!=1 && bpb!=4 && bpb!=8 && bpb!=16))
    throw std::invalid_argument("Invalid original Xenos 2D tile coordinates");
  const uint32_t log_bpb=bpb==1?0:bpb==4?2:bpb==8?3:4;
  // Same 32x32 macro/micro tile address as Xenia's BSD-licensed
  // gpu/texture_address.h::Tiled2D. Coordinates are PIXEL BLOCKS, not pixels.
  const uint64_t outer=(((uint64_t(y)>>5)*(pitch>>5))+(x>>5))<<6;
  const uint64_t inner=(((y>>1)&7u)<<3)|(x&7u);
  const uint64_t v=(outer|inner)<<log_bpb;
  const uint64_t bank=(y>>4)&1u;
  const uint64_t pipe=((x>>3)&3u)^(((y>>3)&1u)<<1);
  return ((uint64_t(y&1u))<<4) | (pipe<<6) | (bank<<11) |
         (v&15u) | (((v>>4)&1u)<<5) |
         (((v>>5)&7u)<<8) | ((v>>8)<<12);
}
PreparedHostImage PrepareExplicitOriginalMips(
    XenosTextureFormat format,
    std::span<const SourceMipLayout> layouts,
    std::span<const std::byte> texture,
    std::span<const std::byte> stream) {
  const auto info=GetTextureBlockInfo(format);
  if(layouts.empty() || layouts.size()>14)
    throw std::invalid_argument("Original Xenos mip chain absent/oversized");
  if(layouts[0].width==0 || layouts[0].height==0 ||
     layouts[0].width>8192 || layouts[0].height>8192)
    throw std::invalid_argument("Invalid original Xenos base texture extent");
  PreparedHostImage image;
  image.format=format;
  image.mips.reserve(layouts.size());
  const uint32_t base_w=layouts[0].width,base_h=layouts[0].height;
  size_t max_mip_count=1;
  for(uint32_t w=base_w,h=base_h;w>1 || h>1;) {
    w=std::max(1u,w>>1);
    h=std::max(1u,h>>1);
    ++max_mip_count;
  }
  if(layouts.size()>max_mip_count)
    throw std::invalid_argument("More original Xenos mip levels than image dimensions allow");
  uint64_t accumulated=0;
  for(size_t i=0;i<layouts.size();++i) {
    const auto& m=layouts[i];
    const uint32_t expected_w=std::max(1u,base_w>>i);
    const uint32_t expected_h=std::max(1u,base_h>>i);
    if(m.width!=expected_w || m.height!=expected_h ||
       !ValidTextureTileMode(m.tiling) ||
       !ValidTextureEndian(m.endian))
      throw std::invalid_argument("Unknown/inconsistent original Xenos mip layout");
    const uint32_t blocks_x=CeilDiv(m.width,info.pixels_per_block);
    const uint32_t blocks_y=CeilDiv(m.height,info.pixels_per_block);
    if(!m.storage_pitch_blocks || !m.storage_height_blocks ||
       m.storage_pitch_blocks>8192 || m.storage_height_blocks>8192 ||
       uint64_t(m.source_x_blocks)+blocks_x>m.storage_pitch_blocks ||
       uint64_t(m.source_y_blocks)+blocks_y>m.storage_height_blocks ||
       (m.tiling==XenosTileMode::Tiled2D &&
        ((m.storage_pitch_blocks%32) || (m.storage_height_blocks%32))))
      throw std::invalid_argument("Original Xenos mip block dimensions out of bounds");
    const uint64_t output_count=uint64_t(blocks_x)*blocks_y*info.bytes_per_block;
    accumulated+=output_count;
    if(accumulated>(512ull<<20) ||
       output_count>std::numeric_limits<size_t>::max())
      throw std::invalid_argument("Unreasonable original Xenos mip memory budget");
    const auto& src=m.from_stream_archive?stream:texture;
    if(m.from_stream_archive && src.empty())
      throw std::invalid_argument("Required original Xbox TexStreams mip source missing");
    if(m.source_offset>src.size())
      throw std::invalid_argument("Original Xenos mip source starts past ARC payload");
    // A mip must be fully backed by its OWN declared tiled/linear storage.
    // Bounds against the whole ARC alone can accidentally read the NEXT mip
    // or the 52-byte original LFXT GPU descriptor as pixels, producing
    // false material matches or corrupt high-resolution host images.
    const uint64_t source_storage_bytes=
        uint64_t(m.storage_pitch_blocks)*m.storage_height_blocks*info.bytes_per_block;
    if(!source_storage_bytes || source_storage_bytes>(512ull<<20) ||
       source_storage_bytes>src.size()-m.source_offset)
      throw std::invalid_argument("Original Xenos mip storage overlaps another ARC record");

    LinearHostMip dst;
    dst.width=m.width;dst.height=m.height;
    dst.row_pitch_bytes=blocks_x*info.bytes_per_block;
    dst.blocks.resize(static_cast<size_t>(output_count));
    for(uint32_t y=0;y<blocks_y;++y) {
      for(uint32_t x=0;x<blocks_x;++x) {
        const uint32_t sx=m.source_x_blocks+x,sy=m.source_y_blocks+y;
        const uint64_t address=m.tiling==XenosTileMode::Tiled2D ?
          OriginalTiled2DOffset(sx,sy,m.storage_pitch_blocks,info.bytes_per_block) :
          (uint64_t(sy)*m.storage_pitch_blocks+sx)*info.bytes_per_block;
        if(address>source_storage_bytes ||
           info.bytes_per_block>source_storage_bytes-address)
          throw std::invalid_argument("Original Xenos tile escapes its own mip allocation");
        const auto* input=src.data()+static_cast<size_t>(m.source_offset+address);
        auto* output=dst.blocks.data()+static_cast<size_t>(
          (uint64_t(y)*blocks_x+x)*info.bytes_per_block);
        ConvertEndian(output,input,info.bytes_per_block,m.endian);
      }
    }
    image.mips.push_back(std::move(dst));
  }
  return image;
}
} // namespace hulk::eor::textures
