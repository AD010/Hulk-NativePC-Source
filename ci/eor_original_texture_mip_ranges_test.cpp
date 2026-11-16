#include "../native_port/src/eor_original_texture_mip_ranges.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::textures;
template<typename F> void Reject(F&& f){
 bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}
 assert(rejected);
}
int main() {
 constexpr size_t kBC1=32*32*8;
 // A 64x64 streamed BC1 mip and a 32x32 resident BC1 mip, both stored
 // as padded 32x32 Xenos blocks. 52 unknown bytes are never interpreted
 // as blocks by the source-range preflight.
 std::vector<std::byte> stream(4+kBC1+52,std::byte{0});
 const uint32_t declared=static_cast<uint32_t>(stream.size()-4);
 for(int i=0;i<4;++i)stream[i]=std::byte(declared>>(24-8*i));
 const size_t inline_at=100;
 std::vector<std::byte> lfxt(inline_at+kBC1+52,std::byte{0});
 OriginalLfxtInlineSection section{};
 section.inline_data_offset=inline_at;
 section.inline_data_bytes=kBC1+52;
 OriginalLfxtMipPlan plan{};
 plan.family=OriginalLfxtStorageFamily::BC_8ByteBlock;
 plan.resident_width=plan.resident_height=32;
 plan.levels={{64,64,kBC1,true},{32,32,kBC1,false}};
 std::vector<SourceMipLayout> layouts(2);
 layouts[0].width=layouts[0].height=64;
 layouts[0].storage_pitch_blocks=32;
 layouts[0].storage_height_blocks=32;
 layouts[0].source_offset=4;
 layouts[0].from_stream_archive=true;
 layouts[0].tiling=XenosTileMode::Tiled2D;
 layouts[0].endian=XenosDataEndian::Swap16;
 layouts[1]=layouts[0];
 layouts[1].width=layouts[1].height=32;
 layouts[1].from_stream_archive=false;
 layouts[1].source_offset=inline_at;
 ValidateOriginalMipPayloadRanges(lfxt,stream,section,plan,
                                 XenosTextureFormat::BC1,layouts);
 auto bad=layouts;
 bad[0].source_offset=0;
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream,section,plan,XenosTextureFormat::BC1,bad);});
 bad=layouts;bad[1].source_offset=inline_at-4;
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream,section,plan,XenosTextureFormat::BC1,bad);});
 bad=layouts;bad[0].storage_height_blocks=64;
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream,section,plan,XenosTextureFormat::BC1,bad);});
 bad=layouts;bad[0].source_x_blocks=4;
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream,section,plan,XenosTextureFormat::BC1,bad);});
 bad=layouts;bad[0].endian=XenosDataEndian::Unknown;
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream,section,plan,XenosTextureFormat::BC1,bad);});
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream,section,plan,XenosTextureFormat::BC3,layouts);});
 auto stream_with_extra=stream;stream_with_extra.push_back(std::byte{1});
 // Even if the caller forges the BE prefix, the source range may no
 // longer consume the required original mip allocation exactly.
 const uint32_t wrong=static_cast<uint32_t>(stream_with_extra.size()-4);
 for(int i=0;i<4;++i)stream_with_extra[i]=std::byte(wrong>>(24-8*i));
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream_with_extra,section,plan,XenosTextureFormat::BC1,layouts);});
 auto bad_section=section;bad_section.inline_data_bytes+=10;
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream,bad_section,plan,XenosTextureFormat::BC1,layouts);});
 auto bad_plan=plan;bad_plan.levels[1].aligned_storage_bytes-=1;
 Reject([&]{ValidateOriginalMipPayloadRanges(
     lfxt,stream,section,bad_plan,XenosTextureFormat::BC1,layouts);});
}
