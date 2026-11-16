#include "../native_port/src/eor_original_texture_gpu_layout.h"
#include <cassert>
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
  Put32(v,base+40,fmt==0x1d?0xd11:0xd10);
}
template<typename F> void Reject(F&& f) {
  bool threw=false;try{f();}catch(const std::invalid_argument&){threw=true;}
  assert(threw);
}
void Case(uint32_t fmt,uint32_t block,
          uint32_t rw,uint32_t rh,
          const std::vector<OriginalLfxtMipLevel>& levels) {
  assert(block==(fmt==0x14?16u:8u));
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
                        OriginalLfxtStorageFamily::Texel_8Byte;
  plan.resident_width=rw;plan.resident_height=rh;plan.levels=levels;
  const auto verified=DecodeOriginalXboxGpuMips(lfxt,stream,section,table,plan);
  assert(verified && verified->layouts.size()==levels.size());
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
} // namespace
int main() {
  // Original 10 authenticated records have exactly three GPU storage codes:
  // Xenos DXT1 (0x12), DXT4_5 (0x14), RGBA16_EXPAND (0x1d).
  Case(0x12,8,64,32,{{128,64,8192,true},{64,32,8192,false},
                       {32,16,8192,false}});
  Case(0x14,16,32,32,{{256,256,65536,true},
                         {128,128,16384,true},{64,64,16384,true},
                         {32,32,16384,false},{16,16,16384,false}});
  Case(0x1d,8,256,256,{{1024,1024,8388608,true},
                          {512,512,2097152,true},
                          {256,256,524288,false}});
}
