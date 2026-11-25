#include "eor_original_texture_gpu_layout.h"
#include "eor_texture_format_info.h"
#include "eor_original_texture_mip_ranges.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
namespace hulk::eor::textures {
namespace {
constexpr size_t kDescriptorBytes=52;
uint32_t BE32(std::span<const std::byte> bytes, size_t pos) {
  if(pos>bytes.size() || bytes.size()-pos<4)
    throw std::invalid_argument("Truncated authentic Xbox 360 texture descriptor");
  return uint32_t(std::to_integer<uint8_t>(bytes[pos]))<<24 |
         uint32_t(std::to_integer<uint8_t>(bytes[pos+1]))<<16 |
         uint32_t(std::to_integer<uint8_t>(bytes[pos+2]))<<8 |
         uint32_t(std::to_integer<uint8_t>(bytes[pos+3]));
}
bool KnownDescriptor(std::span<const std::byte> full,
                     XenosTextureFormat& fmt, uint32_t& w,uint32_t& h) {
  if(full.size()<kDescriptorBytes)return false;
  const auto data=full.last(kDescriptorBytes);
  // Confirmed in 20 real trailer instances (10 LFXT and 10 TexStreams).
  if(BE32(data,0)!=3 || BE32(data,4)!=1 ||
     BE32(data,8)!=0 || BE32(data,12)!=0 || BE32(data,16)!=0 ||
     BE32(data,20)!=0xffff0000u || BE32(data,24)!=0xffff0000u)
    return false;
  const uint32_t id=BE32(data,32);
  switch(id) {
    case 0x12: fmt=XenosTextureFormat::BC1;break; // k_DXT1
    case 0x14: fmt=XenosTextureFormat::BC3;break; // k_DXT4_5
    case 0x1d: fmt=XenosTextureFormat::RGBA16F;break; // k_16_16_16_16_EXPAND
    case 0x06: fmt=XenosTextureFormat::RGBA8;break; // k_8_8_8_8
    case 0x02: fmt=XenosTextureFormat::R8;break; // k_8
    default:return false;
  }
  if(BE32(data,40)!=(id==0x1d ? 0x00000d11u :
                      id==0x06 ? 0x00000c14u : 0x00000d10u))
    return false;
  const uint32_t dimensions=BE32(data,36);
  w=(dimensions&0x1fffu)+1;
  h=((dimensions>>13)&0x1fffu)+1;
  return w!=0 && h!=0 && w<=8192 && h<=8192 &&
         (dimensions>>26)==0;
}
uint32_t PaddedBlocks(uint32_t texels, uint32_t block) {
  const uint64_t count=(uint64_t(texels)+block-1)/block;
  if(count>8192u)return 0;
  return static_cast<uint32_t>((count+31u)&~uint64_t(31));
}
} // namespace
std::optional<VerifiedOriginalXboxCube> DecodeOriginalStandaloneCube(
    std::span<const std::byte> lfxt,uint32_t original_hash,
    const OriginalLfxtInlineSection& section) {
  // All eleven source USA environment records are BC3, one mip, six faces.
  // Validate the authentic header AND descriptor before exposing pixels.
  const auto verified=FindOriginalLfxtInlineSection(lfxt,original_hash);
  if(!verified || verified->inline_data_offset!=section.inline_data_offset ||
     verified->inline_data_bytes!=section.inline_data_bytes ||
     section.inline_data_bytes<=kDescriptorBytes)return {};
  const auto end=std::find(lfxt.begin()+4,lfxt.end(),std::byte{0});
  const size_t base=static_cast<size_t>(end-lfxt.begin())+1;
  if(verified->outer_length_field_offset!=base+22 ||
     lfxt[base+21]!=std::byte{0} || BE32(lfxt,base+13)!=1)return {};
  const auto data=lfxt.last(kDescriptorBytes);
  if(BE32(data,0)!=3 || BE32(data,4)!=1 ||
     BE32(data,8)!=0 || BE32(data,12)!=0 || BE32(data,16)!=0 ||
     BE32(data,20)!=0xffff0000u || BE32(data,24)!=0xffff0000u ||
     BE32(data,32)!=20u || BE32(data,40)!=0xd10u ||
     BE32(data,44)!=0 || BE32(data,48)!=0xe00u)return {};
  const uint32_t shape=BE32(data,36),word0=BE32(data,28);
  const uint32_t width=(shape&8191u)+1u,height=((shape>>13)&8191u)+1u;
  if((shape>>26)!=5u || width!=height || width<32 || width>256 ||
     (width&(width-1)) || (word0&3u)!=2u || !(word0>>31) ||
     (BE32(lfxt,base+1)&0xffffu)!=width ||
     (BE32(lfxt,base+5)&0xffffu)!=height)return {};
  const uint32_t pitch=PaddedBlocks(width,4),rows=PaddedBlocks(height,4);
  const uint64_t face_bytes=uint64_t(pitch)*rows*16u;
  const uint64_t payload=section.inline_data_bytes-kDescriptorBytes;
  if(!face_bytes || ((word0>>22)&511u)*32u!=pitch*4u ||
     face_bytes*6u!=payload || BE32(lfxt,base+17)!=payload ||
     section.inline_data_offset+payload!=lfxt.size()-kDescriptorBytes)return {};
  VerifiedOriginalXboxCube cube;
  cube.format=XenosTextureFormat::BC3;
  for(size_t face=0;face<cube.faces.size();++face) {
    auto& level=cube.faces[face];
    level.width=width;level.height=height;
    level.source_offset=section.inline_data_offset+face*face_bytes;
    level.storage_pitch_blocks=pitch;level.storage_height_blocks=rows;
    level.tiling=XenosTileMode::Tiled2D;level.endian=XenosDataEndian::NoSwap;
  }
  return cube;
}
std::optional<std::pair<OriginalLfxtMipPlan,VerifiedOriginalXboxGpuMipChain>>
DecodeOriginalStandaloneMip(std::span<const std::byte> lfxt, uint32_t original_hash,
                            const OriginalLfxtInlineSection& section) {
  const auto verified=FindOriginalLfxtInlineSection(lfxt,original_hash);
  if (!verified || verified->inline_data_offset!=section.inline_data_offset ||
      verified->inline_data_bytes!=section.inline_data_bytes ||
      section.inline_data_bytes<=kDescriptorBytes) return std::nullopt;
  const auto end=std::find(lfxt.begin()+4,lfxt.end(),std::byte{0});
  const size_t base=static_cast<size_t>(end-lfxt.begin())+1;
  // No stream table: one local allocation with an explicit byte count.
  if (verified->outer_length_field_offset!=base+22 || lfxt[base+21]!=std::byte{0} ||
      BE32(lfxt,base+13)!=1) return std::nullopt;
  const uint64_t payload=section.inline_data_bytes-kDescriptorBytes;
  if (BE32(lfxt,base+17)!=payload) return std::nullopt;
  XenosTextureFormat format;
  uint32_t width=0,height=0;
  if (!KnownDescriptor(lfxt,format,width,height) || format==XenosTextureFormat::RGBA16F)
    return std::nullopt;
  const auto fetch=lfxt.last(kDescriptorBytes);
  const uint32_t word0=BE32(fetch,28);
  const uint32_t source_width=BE32(lfxt,base+1)&0xffffu;
  const uint32_t source_height=BE32(lfxt,base+5)&0xffffu;
  if (source_width!=width || source_height!=height || (word0&3u)!=2u ||
      !(word0>>31) || BE32(fetch,44)!=0u || BE32(fetch,48)!=0xa00u)
    return std::nullopt;
  const auto info=GetTextureBlockInfo(format);
  const uint32_t block=info.pixels_per_block,bytes=info.bytes_per_block;
  // The sole source-proven k_8 asset uses a complete 256x256 allocation.
  // Smaller k_8 tiled images require different padded storage rules.
  if(format==XenosTextureFormat::R8 && (width!=256u || height!=256u || (word0&1023u)!=2u))return {};
  const auto pitch=PaddedBlocks(width,block), rows=PaddedBlocks(height,block);
  if (((word0>>22)&511u)*32u!=pitch*block || uint64_t(pitch)*rows*bytes!=payload)
    return std::nullopt;
  const auto family=info.family;
  OriginalLfxtMipPlan plan{family,width,height,{{width,height,static_cast<uint32_t>(payload),false}},true};
  SourceMipLayout level{};
  level.width=width;level.height=height;level.source_offset=section.inline_data_offset;
  level.storage_pitch_blocks=pitch;level.storage_height_blocks=rows;
  level.tiling=XenosTileMode::Tiled2D;level.endian=XenosDataEndian::NoSwap;
  VerifiedOriginalXboxGpuMipChain gpu{format,{level}};
  ValidateOriginalMipPayloadRanges(lfxt,{},section,plan,format,gpu.layouts);
  return std::pair{std::move(plan),std::move(gpu)};
}
std::optional<VerifiedOriginalXboxGpuMipChain> DecodeOriginalXboxGpuMips(
    std::span<const std::byte> lfxt,std::span<const std::byte> stream,
    const OriginalLfxtInlineSection& section,
    const OriginalLfxtStreamTable& table,
    const OriginalLfxtMipPlan& plan) {
  if(lfxt.size()<kDescriptorBytes || stream.size()<kDescriptorBytes+4 ||
     !section.inline_data_bytes || !section.inline_data_offset ||
     section.inline_data_offset>lfxt.size()-kDescriptorBytes ||
     section.inline_data_bytes!=lfxt.size()-section.inline_data_offset ||
     plan.levels.empty() || plan.levels.size()>14 ||
     table.levels.empty() || table.levels.size()>=plan.levels.size())
    return std::nullopt;
  // The original Xbox archive uses a 32-bit big-endian size of its
  // remaining record. The last 52 bytes are the actual GPU descriptor.
  (void)ValidateOriginalTexStreamEnvelope(stream);
  XenosTextureFormat resident_format=XenosTextureFormat::Unknown;
  XenosTextureFormat stream_format=XenosTextureFormat::Unknown;
  uint32_t rw=0,rh=0,sw=0,sh=0;
  if(!KnownDescriptor(lfxt,resident_format,rw,rh) ||
     !KnownDescriptor(stream,stream_format,sw,sh) ||
     resident_format!=stream_format || resident_format==XenosTextureFormat::R8 ||
     rw!=plan.resident_width || rh!=plan.resident_height ||
     sw!=plan.levels.front().width || sh!=plan.levels.front().height)
    return std::nullopt;
  // BC1/BC3 are 4x4 pixel blocks; RGBA8 and RGBA16F are uncompressed
  // per-texel formats. Treating RGBA8 as BC undercounts its tiled footprint
  // by up to 16x and rejects or misaddresses the original 0x06 LFXT mips.
  const auto info=GetTextureBlockInfo(resident_format);
  const uint32_t block_pixels=info.pixels_per_block;
  const uint32_t block_bytes=info.bytes_per_block;
  const auto family=info.family;
  if(plan.family!=family ||
     table.levels.size()!=static_cast<size_t>(std::count_if(
        plan.levels.begin(),plan.levels.end(),
        [](const OriginalLfxtMipLevel& l){return l.from_stream_archive;})))
    return std::nullopt;
  uint64_t stream_cursor=4;
  uint64_t resident_cursor=section.inline_data_offset;
  const uint64_t stream_end=stream.size()-kDescriptorBytes;
  const uint64_t resident_end=lfxt.size()-kDescriptorBytes;
  VerifiedOriginalXboxGpuMipChain result;
  result.format=resident_format;
  result.layouts.reserve(plan.levels.size());
  bool entered_resident=false;
  for(size_t i=0;i<plan.levels.size();++i) {
    const auto& mip=plan.levels[i];
    const uint32_t pitch=PaddedBlocks(mip.width,block_pixels);
    const uint32_t height=PaddedBlocks(mip.height,block_pixels);
    const uint64_t required=uint64_t(pitch)*height*block_bytes;
    if(!pitch || !height || !required ||
       required!=mip.aligned_storage_bytes ||
       (i<table.levels.size() &&
        mip.aligned_storage_bytes!=table.levels[i].source_aligned_bytes))
      return std::nullopt;
    SourceMipLayout layout{};
    layout.from_stream_archive=mip.from_stream_archive;
    layout.width=mip.width;
    layout.height=mip.height;
    layout.storage_pitch_blocks=pitch;
    layout.storage_height_blocks=height;
    layout.tiling=XenosTileMode::Tiled2D;
    // Real BC1/BC3 Xenos source blocks decoded legibly WITHOUT byte swap
    // in all eight user-supplied compressed texture specimens.
    // The two original EXPAND lightmaps likewise decode as finite half floats.
    layout.endian=XenosDataEndian::NoSwap;
    if(mip.from_stream_archive) {
      if(entered_resident || stream_cursor>stream_end ||
         required>stream_end-stream_cursor)return std::nullopt;
      layout.source_offset=stream_cursor;
      stream_cursor+=required;
    } else {
      entered_resident=true;
      if(resident_cursor>resident_end ||
         required>resident_end-resident_cursor)return std::nullopt;
      layout.source_offset=resident_cursor;
      resident_cursor+=required;
    }
    result.layouts.push_back(layout);
  }
  if(stream_cursor!=stream_end || resident_cursor!=resident_end)
    return std::nullopt;
  // Range validator independently rejects insufficient/stale/overlapping
  // original payloads before making a host GPU allocation.
  ValidateOriginalMipPayloadRanges(lfxt,stream,section,plan,
                                   result.format,result.layouts);
  return result;
}
} // namespace hulk::eor::textures
