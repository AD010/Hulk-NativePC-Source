#include "../native_port/src/eor_xenos_texture_mips.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::textures;
namespace {
template<typename F> void Reject(F f) {
  bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}
  assert(failed);
}
}
int main() {
  assert(OriginalTiled2DOffset(0,0,64,8)==0);
  assert(OriginalTiled2DOffset(1,0,64,8)==8);
  assert(OriginalTiled2DOffset(0,1,64,8)==16);
  assert(OriginalTiled2DOffset(0,2,64,8)==512);
  assert(OriginalTiled2DOffset(32,0,64,8)==8192);
  assert(OriginalTiled2DOffset(0,32,64,8)==16384);
  assert(OriginalTiled2DOffset(1,0,64,16)==32);
  assert(OriginalTiled2DOffset(0,2,64,16)==1024);
  Reject([]{(void)OriginalTiled2DOffset(0,0,31,8);});
  // A padded 32x32 Xenos DXT1 block page with 16x16 sampled blocks.
  std::vector<std::byte> tiled(32u*32u*8u);
  for(uint32_t y=0;y<16;++y)for(uint32_t x=0;x<16;++x) {
    auto p=OriginalTiled2DOffset(x,y,32,8);
    for(uint32_t b=0;b<8;++b)
      tiled[static_cast<size_t>(p+b)]=std::byte((x+y*16+b)&255);
  }
  SourceMipLayout level;
  level.width=64;level.height=64;
  level.storage_pitch_blocks=level.storage_height_blocks=32;
  level.tiling=XenosTileMode::Tiled2D;level.endian=XenosDataEndian::NoSwap;
  auto result=PrepareExplicitOriginalMips(XenosTextureFormat::BC1,
       std::span(&level,1),tiled,{});
  assert(result.mips.size()==1 && result.mips[0].row_pitch_bytes==128 &&
         result.mips[0].blocks.size()==2048);
  for(uint32_t y=0;y<16;++y)for(uint32_t x=0;x<16;++x)
    for(uint32_t b=0;b<8;++b)
      assert(result.mips[0].blocks[(y*16+x)*8+b]==std::byte((x+y*16+b)&255));
  // Explicit packed tail source origin: only a single BC3 block at offset
  // (5,7) inside a shared 32x32 tiled page. Guest endian mode is mandatory.
  std::vector<std::byte> packed(32u*32u*16u);
  auto pos=OriginalTiled2DOffset(5,7,32,16);
  for(uint32_t i=0;i<16;++i)packed[static_cast<size_t>(pos+i)]=std::byte(i);
  SourceMipLayout tail;
  tail.from_stream_archive=true;tail.width=4;tail.height=4;
  tail.storage_pitch_blocks=32;tail.storage_height_blocks=32;
  tail.source_x_blocks=5;tail.source_y_blocks=7;
  tail.tiling=XenosTileMode::Tiled2D;tail.endian=XenosDataEndian::Swap16;
  auto bc3=PrepareExplicitOriginalMips(XenosTextureFormat::BC3,
      std::span(&tail,1),{},packed);
  for(uint32_t i=0;i<16;++i)
    assert(bc3.mips[0].blocks[i]==std::byte(i^1));
  tail.endian=XenosDataEndian::Unknown;
  Reject([&]{(void)PrepareExplicitOriginalMips(XenosTextureFormat::BC3,
      std::span(&tail,1),{},packed);});
  tail.endian=XenosDataEndian::Swap16;tail.source_x_blocks=33;
  Reject([&]{(void)PrepareExplicitOriginalMips(XenosTextureFormat::BC3,
      std::span(&tail,1),{},packed);});
  // Literal consecutive BC1 8x8, 4x4 levels with no invented lower mip.
  std::vector<std::byte> linear(40);
  for(uint32_t i=0;i<linear.size();++i)linear[i]=std::byte(i);
  SourceMipLayout m0;
  m0.width=8;m0.height=8;m0.storage_pitch_blocks=2;
  m0.storage_height_blocks=2;m0.tiling=XenosTileMode::Linear;
  m0.endian=XenosDataEndian::NoSwap;
  SourceMipLayout m1=m0;
  m1.width=4;m1.height=4;m1.source_offset=32;
  auto chain=PrepareExplicitOriginalMips(XenosTextureFormat::BC1,
      std::vector<SourceMipLayout>{m0,m1},linear,{});
  assert(chain.mips.size()==2 && chain.mips[0].blocks.size()==32 &&
         chain.mips[1].blocks.size()==8 && chain.mips[1].blocks[0]==std::byte(32));
  m1.width=2;
  Reject([&]{(void)PrepareExplicitOriginalMips(XenosTextureFormat::BC1,
      std::vector<SourceMipLayout>{m0,m1},linear,{});});
  m1.width=4;m1.source_offset=39;
  Reject([&]{(void)PrepareExplicitOriginalMips(XenosTextureFormat::BC1,
      std::vector<SourceMipLayout>{m0,m1},linear,{});});
  Reject([&]{(void)PrepareExplicitOriginalMips(XenosTextureFormat::Unknown,
      std::vector<SourceMipLayout>{m0},linear,{});});
}
