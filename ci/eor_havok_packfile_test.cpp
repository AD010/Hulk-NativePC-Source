#include "../native_port/src/eor_havok_packfile.h"
#include <cassert>
#include <stdexcept>
#include <string_view>
#include <vector>
using namespace hulk::eor::havok;
namespace {
void Put32(std::vector<std::byte>& b,size_t p,uint32_t v,bool be) {
  for(unsigned i=0;i<4;++i)
    b.at(p+i)=std::byte((v>>(8*(be?3-i:i)))&0xffu);
}
std::vector<std::byte> Fixture(bool xbox,uint32_t hash=0xAABBAA11u) {
  std::vector<std::byte> b(400);
  const size_t m=xbox?29:34;
  const unsigned char magic[]{0x57,0xe0,0xe0,0x57,0x10,0xc0,0xc0,0x10};
  for(size_t i=0;i<8;++i)b[m+i]=std::byte{magic[i]};
  Put32(b,m+8,hash,xbox);Put32(b,m+12,5,xbox);
  b[m+16]=std::byte{4};
  b[m+17]=std::byte{static_cast<unsigned char>(xbox?0:1)};
  b[m+18]=std::byte{0};b[m+19]=std::byte{1};
  Put32(b,m+20,3,xbox);
  constexpr std::string_view version{"Havok-5.1.0-r1"};
  for(size_t i=0;i<version.size();++i)
    b[m+40+i]=std::byte{static_cast<unsigned char>(version[i])};
  constexpr std::string_view names[]{"__classnames__","__data__","__types__"};
  for(size_t j=0;j<3;++j) {
    const size_t at=m+64+j*48;
    for(size_t i=0;i<names[j].size();++i)
      b[at+i]=std::byte{static_cast<unsigned char>(names[j][i])};
    const uint32_t start=static_cast<uint32_t>(208+j*16);
    Put32(b,at+20,start,xbox);
    for(size_t k=1;k<7;++k) Put32(b,at+20+k*4,j==0?16u:0u,xbox);
  }
  // A real three-kind fixup graph in the synthetic data section.
  const size_t section=m+224;
  const size_t descriptor=m+64+48;
  Put32(b,descriptor+24,16,xbox);
  Put32(b,descriptor+28,32,xbox);
  Put32(b,descriptor+32,48,xbox);
  Put32(b,descriptor+36,64,xbox);
  Put32(b,descriptor+40,64,xbox);
  Put32(b,descriptor+44,64,xbox);
  Put32(b,m+64+48*2+20,288,xbox);
  // Local pair (0 -> 8), one sentinel.
  Put32(b,section+16,0,xbox);Put32(b,section+20,8,xbox);
  Put32(b,section+24,0xffffffffu,xbox);Put32(b,section+28,0xffffffffu,xbox);
  // Global triple (4 -> section 1:8), alignment padding.
  Put32(b,section+32,4,xbox);Put32(b,section+36,1,xbox);
  Put32(b,section+40,8,xbox);Put32(b,section+44,0xffffffffu,xbox);
  // Virtual class triple (8 -> section 0:0), alignment padding.
  Put32(b,section+48,8,xbox);Put32(b,section+52,0,xbox);
  Put32(b,section+56,0,xbox);Put32(b,section+60,0xffffffffu,xbox);
  return b;
}
template<class Fn> void MustThrow(Fn fn) {
  bool rejected=false;
  try{fn();}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
}
} // namespace
int main() {
  for(bool xbox:{false,true}) {
    const auto plat=xbox?Platform::Xbox360:Platform::PC;
    auto b=Fixture(xbox);
    const auto parsed=Inspect(b,0xAABBAA11u,plat);
    assert(parsed.header_offset==(xbox?29u:34u));
    assert(parsed.file_version==5&&parsed.sections==3);
    assert(parsed.platform==plat&&parsed.payload_bytes==400);
    assert(parsed.section_table[0].absolute_start==208);
    assert(parsed.section_table[1].absolute_start==224);
    const auto edges=DecodeFixups(b,0xAABBAA11u,plat);
    assert(edges.size()==3);
    assert(edges[0].kind==FixupKind::Local &&
           edges[0].to_section==1 && edges[0].to_offset==8);
    assert(edges[1].kind==FixupKind::Global && edges[1].to_section==1);
    assert(edges[2].kind==FixupKind::VirtualClass && edges[2].to_section==0);
    MustThrow([&]{(void)Inspect(b,0xAABBAA10u,plat);});
    MustThrow([&]{(void)Inspect(b,0xAABBAA11u,xbox?Platform::PC:Platform::Xbox360);});
    b[(xbox?29u:34u)+44]=std::byte{0};
    MustThrow([&]{(void)Inspect(b,0xAABBAA11u,plat);});
  }
  auto truncated=Fixture(true);truncated.resize(80);
  MustThrow([&]{(void)Inspect(truncated,0xAABBAA11u,Platform::Xbox360);});
}
