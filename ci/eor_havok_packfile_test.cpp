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
  std::vector<std::byte> b(320);
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
    for(size_t k=1;k<7;++k) Put32(b,at+20+k*4,j<2?16u:0u,xbox);
  }
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
    assert(parsed.platform==plat&&parsed.payload_bytes==320);
    assert(parsed.section_table[0].absolute_start==208);
    assert(parsed.section_table[1].absolute_start==224);
    MustThrow([&]{(void)Inspect(b,0xAABBAA10u,plat);});
    MustThrow([&]{(void)Inspect(b,0xAABBAA11u,xbox?Platform::PC:Platform::Xbox360);});
    b[(xbox?29u:34u)+44]=std::byte{0};
    MustThrow([&]{(void)Inspect(b,0xAABBAA11u,plat);});
  }
  auto truncated=Fixture(true);truncated.resize(80);
  MustThrow([&]{(void)Inspect(truncated,0xAABBAA11u,Platform::Xbox360);});
}
