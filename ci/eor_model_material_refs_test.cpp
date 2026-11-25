#include "../native_port/src/eor_model_material_refs.h"
#include "../native_port/src/eor_model_binary.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor;
namespace {
void Word(std::vector<std::byte>& d,uint32_t v,bool be) {
  for(int k=0;k<4;++k)d.push_back(std::byte((v>>(8*(be?3-k:k)))&255));
}
void Set(std::vector<std::byte>& d,size_t at,uint32_t v,bool be) {
  for(int k=0;k<4;++k)d[at+k]=std::byte((v>>(8*(be?3-k:k)))&255);
}
std::vector<std::byte> Model(bool be) {
  std::vector<std::byte> d{std::byte(be?0:1),std::byte(be?1:0)};
  d.insert(d.end(),4,std::byte(0));
  for(char c:std::string("city_model"))d.push_back(std::byte(c));
  d.push_back(std::byte(0));d.push_back(std::byte(0));Word(d,1,be);
  for(char c:std::string("blinn1"))d.push_back(std::byte(c));
  d.push_back(std::byte(0));
  const auto data=d.size();d.resize(data+192);
  Set(d,data+(be?56u:48u),ResourceHash("StreetTexture"),be);
  if(be)Set(d,data+60,ResourceHash("NormalTexture"),be);
  return d;
}
std::vector<std::byte> IndexData(bool be) {
  std::vector<std::byte> d;Word(d,2,be);
  Word(d,0,be);Word(d,0,be);Word(d,0,be);
  const auto name=uint32_t(d.size());
  for(char c:std::string("Textures"))d.push_back(std::byte(c));
  d.push_back(std::byte(0));const auto table=uint32_t(d.size());
  uint32_t a=ResourceHash("StreetTexture"),b=ResourceHash("NormalTexture");
  Word(d,2,be);
  if(a<b){Word(d,a,be);Word(d,b,be);}else{Word(d,b,be);Word(d,a,be);}
  Word(d,4096,be);Word(d,1024,be);Word(d,0,be);
  Word(d,5120,be);Word(d,2048,be);Word(d,0,be);
  const auto footer=uint32_t(d.size());Word(d,0,be);
  Set(d,4,name,be);Set(d,8,table,be);Set(d,12,footer,be);return d;
}
}
int main() {
  using hulk::eor::model::Platform;
  using namespace hulk::eor::model::binary;
  constexpr std::array<std::byte,5> sample{std::byte{0xff},std::byte{0x12},
      std::byte{0x34},std::byte{0x56},std::byte{0x78}};
  assert(U32(sample,1,Platform::Xbox360)==0x12345678u);
  assert(U32(sample,1,Platform::PcD3d9)==0x78563412u);
  assert(BE32(sample,1)==0x12345678u);
  assert(TextureSlots(Platform::Xbox360).size()==7 &&
         TextureSlots(Platform::Xbox360).front()==56 &&
         TextureSlots(Platform::Xbox360).back()==80);
  assert(TextureSlots(Platform::PcD3d9).size()==2 &&
         TextureSlots(Platform::PcD3d9).front()==48 &&
         TextureSlots(Platform::PcD3d9).back()==52);
  for(bool be:{false,true}) {
    auto bytes=Model(be);const auto index=Index::Decode(IndexData(be));
    const auto refs=model::FirstSurfaceTextureCandidates(bytes,index);
    assert(refs.size()==(be?2u:1u));
    assert(refs[0].offset_from_surface_data==(be?56u:48u));
    assert(refs[0].original_texture_hash==ResourceHash("StreetTexture"));
    assert(refs[0].original_resource.size>0);
    if(be)assert(refs[1].original_texture_hash==ResourceHash("NormalTexture"));
    bytes[0]=std::byte(0x7f);
    bool rejected=false;
    try{(void)model::FirstSurfaceTextureCandidates(bytes,index);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    bytes=Model(be);
    const auto mismatch=Index::Decode(IndexData(!be));
    rejected=false;
    try{(void)model::FirstSurfaceTextureCandidates(bytes,mismatch);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
  }
}
