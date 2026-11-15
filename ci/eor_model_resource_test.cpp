#include "../native_port/src/eor_model_resource.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor::model;
static void Put32(std::vector<std::byte>& d,uint32_t x,bool xbox) {
  for(int i=0;i<4;++i) d.push_back(std::byte((x >> (8*(xbox ? 3-i:i))) & 0xff));
}
static std::vector<std::byte> Sample(bool xbox,bool surface,uint8_t kind=0) {
  std::vector<std::byte> d {std::byte(xbox?0:1),std::byte(xbox?1:0)};
  d.insert(d.end(),4,std::byte(0));
  for(char c:std::string("city_block_lod2"))d.push_back(std::byte(c));
  d.push_back(std::byte(0));d.push_back(std::byte(kind));
  Put32(d,surface?2:0,xbox);
  if(surface) {
    for(char c:std::string("Storefront_LodChipsMidtown_blinn2"))d.push_back(std::byte(c));
    d.push_back(std::byte(0));
  }
  d.insert(d.end(),40,std::byte(0));return d;
}
int main() {
  for(bool xbox:{false,true}) {
    auto data=Sample(xbox,true);
    auto r=Inspect(data);
    assert(r.platform==(xbox?Platform::Xbox360:Platform::PcD3d9));
    assert(r.model_name=="city_block_lod2" && r.model_kind==0 && r.surface_records==2);
    assert(r.first_surface_name=="Storefront_LodChipsMidtown_blinn2");
    assert(r.surface_data_offset<data.size());
    r=Inspect(Sample(xbox,true,3));assert(r.model_kind==3);
    r=Inspect(Sample(xbox,false));assert(r.surface_records==0 && r.first_surface_name.empty());
    data[0]=std::byte(0xAB);
    bool rejected=false;try{Inspect(data);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
  }
  auto data=Sample(true,true);data[6]=std::byte('/');
  bool rejected=false;try{Inspect(data);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
  data=Sample(true,true);data.resize(20);rejected=false;
  try{Inspect(data);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
}
