#include "../native_port/src/eor_original_texture_gpu_layout.h"
#include <cassert>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor::textures;
namespace {
void Put32(std::vector<std::byte>& v,size_t at,uint32_t n) {
  for(size_t i=0;i<4;++i)
    v[at+i]=std::byte(n>>(24-8*i));
}
void Trailer(std::vector<std::byte>& v,uint32_t fmt,uint32_t w,uint32_t h) {
  const size_t base=v.size()-52;
  Put32(v,base,3);Put32(v,base+4,1);
  Put32(v,base+20,0xffff0000u);Put32(v,base+24,0xffff0000u);
  Put32(v,base+32,fmt);
  Put32(v,base+36,(w-1)|((h-1)<<13));
  Put32(v,base+40,fmt==0x1d?0xd11:fmt==0x06?0xc14:0xd10);
}
template<typename F> void Reject(F&& f) {
  bool threw=false;try{f();}catch(const std::invalid_argument&){threw=true;}
  assert(threw);
}
void Case(uint32_t fmt,uint32_t block,
          uint32_t rw,uint32_t rh,
          const std::vector<OriginalLfxtMipLevel>& levels) {
  assert(block==(fmt==0x14?16u:fmt==0x06?4u:8u));
  uint64_t stream_bytes=0,inline_bytes=0;
  for(const auto& l:levels)(l.from_stream_archive?stream_bytes:inline_bytes)+=
      l.aligned_storage_bytes;
  const size_t offset=120;
  std::vector<std::byte> lfxt(offset+inline_bytes+52);
  std::vector<std::byte> stream(4+stream_bytes+52);
  Put32(stream,0,static_cast<uint32_t>(stream.size()-4));
  Trailer(lfxt,fmt,rw,rh);
  Trailer(stream,fmt,levels.front().width,levels.front().height);
  OriginalLfxtInlineSection section{};
  section.inline_data_offset=offset;
  section.inline_data_bytes=inline_bytes+52;
  OriginalLfxtStreamTable table{};
  for(const auto& l:levels)if(l.from_stream_archive)
      table.levels.push_back({l.aligned_storage_bytes,1.0f});
  OriginalLfxtMipPlan plan{};
  plan.family=fmt==0x12?OriginalLfxtStorageFamily::BC_8ByteBlock:
              fmt==0x14?OriginalLfxtStorageFamily::BC_16ByteBlock:
              fmt==0x06?OriginalLfxtStorageFamily::Texel_4Byte:
                        OriginalLfxtStorageFamily::Texel_8Byte;
  plan.resident_width=rw;plan.resident_height=rh;plan.levels=levels;
  const auto verified=DecodeOriginalXboxGpuMips(lfxt,stream,section,table,plan);
  assert(verified && verified->layouts.size()==levels.size());
  // Uncompressed 0x06/0x1D mips are 1 pixel per GPU storage unit, unlike
  // BC1/BC3 whose 4x4 blocks are 8/16 bytes. Incorrect RGBA8 decoding
  // previously undercounted a 1024x1024 source level by 16x.
  if (fmt==0x06) {
    assert(verified->format==XenosTextureFormat::RGBA8);
    assert(verified->layouts[0].storage_pitch_blocks==1024);
    assert(verified->layouts[0].storage_height_blocks==1024);
    assert(uint64_t(verified->layouts[0].storage_pitch_blocks) *
           verified->layouts[0].storage_height_blocks * 4u ==
           levels[0].aligned_storage_bytes);
  }
  if (fmt==0x1d) {
    assert(verified->format==XenosTextureFormat::RGBA16F);
    assert(verified->layouts[0].storage_pitch_blocks==1024);
  }
  assert(verified->layouts[0].source_offset==4 &&
         verified->layouts[0].tiling==XenosTileMode::Tiled2D &&
         verified->layouts[0].endian==XenosDataEndian::NoSwap);
  assert(verified->layouts.back().source_offset>=offset);
  auto bad=stream;Put32(bad,bad.size()-52+32,0x13);
  assert(!DecodeOriginalXboxGpuMips(lfxt,bad,section,table,plan));
  bad=stream;Put32(bad,bad.size()-52+36,0);
  assert(!DecodeOriginalXboxGpuMips(lfxt,bad,section,table,plan));
  bad=stream;bad[bad.size()-52]=std::byte{1};
  assert(!DecodeOriginalXboxGpuMips(lfxt,bad,section,table,plan));
  bad=stream;bad.push_back(std::byte{0});
  Put32(bad,0,static_cast<uint32_t>(bad.size()-4));
  assert(!DecodeOriginalXboxGpuMips(lfxt,bad,section,table,plan));
  bad=stream;Put32(bad,0,1);
  Reject([&]{(void)DecodeOriginalXboxGpuMips(lfxt,bad,section,table,plan);});
}
void CheckStandalone() {
  constexpr char name[]="Atlas";
  constexpr uint32_t width=64,height=32,extent=width*height*4;
  constexpr size_t base=4+sizeof(name),outer=base+22,begin=outer+8;
  std::vector<std::byte> bytes(begin+extent+52);
  std::memcpy(bytes.data(),"LFXT",4);
  std::memcpy(bytes.data()+4,name,sizeof(name));
  Put32(bytes,base+1,width);Put32(bytes,base+5,height);
  Put32(bytes,base+13,1);Put32(bytes,base+17,extent);
  Put32(bytes,outer,extent+56);Put32(bytes,outer+4,extent+52);
  Trailer(bytes,6,width,height);
  const auto footer=begin+extent;
  Put32(bytes,footer+28,0x80800002);Put32(bytes,footer+48,0xa00);
  const auto hash=hulk::eor::ResourceHash(name);
  auto section=FindOriginalLfxtInlineSection(bytes,hash);assert(section);
  auto decode=[&] {return DecodeOriginalStandaloneMip(bytes,hash,*section);};
  const auto mip=decode();
  assert(mip && mip->first.absolute_source_offsets_proven && mip->second.layouts.size()==1);
  assert(mip->second.layouts[0].source_offset==begin && !mip->second.layouts[0].from_stream_archive);
  Put32(bytes,footer+44,1u<<6);assert(!decode());Put32(bytes,footer+44,0);
  Put32(bytes,footer+28,0x80400002);assert(!decode());Put32(bytes,footer+28,0x80800002);
  bytes[base+21]=std::byte{1};assert(!decode());bytes[base+21]=std::byte{};
  Put32(bytes,base+17,extent-4);assert(!decode());
}
} // namespace
int main() {
  CheckStandalone();
  // GPU descriptor fixture coverage: DXT1 (0x12), DXT4_5 (0x14),
  // RGBA8 (0x06; synthetic regression), RGBA16_EXPAND (0x1d).
  // A synthetic format fixture is not evidence of a live Hulk Draw binding.
  Case(0x12,8,64,32,{{128,64,8192,true},{64,32,8192,false},
                       {32,16,8192,false}});
  Case(0x14,16,32,32,{{256,256,65536,true},
                         {128,128,16384,true},{64,64,16384,true},
                         {32,32,16384,false},{16,16,16384,false}});
  Case(0x06,4,256,256,{{1024,1024,4194304,true},
                          {512,512,1048576,true},
                          {256,256,262144,false}});
  Case(0x1d,8,256,256,{{1024,1024,8388608,true},
                          {512,512,2097152,true},
                          {256,256,524288,false}});
}
