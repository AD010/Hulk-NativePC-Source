#include "../native_port/src/eor_model_surfaces.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor::model;
static void Word(std::vector<std::byte>& d,uint32_t x,bool be) {
 for(int i=0;i<4;++i)d.push_back(std::byte(x>>(8*(be?3-i:i))));
}
static void Add(std::vector<std::byte>& d,const std::string& name,bool be,uint32_t mask) {
 for(char c:name)d.push_back(std::byte(c));
 d.push_back(std::byte(0));Word(d,0,be);Word(d,2,be);Word(d,mask,be);
 d.insert(d.end(),60,std::byte(0));
}
static std::vector<std::byte> Model(bool be) {
 std::vector<std::byte> d{std::byte(be?0:1),std::byte(be?1:0)};
 d.insert(d.end(),4,std::byte(0));
 for(char c:std::string("hulk_mesh"))d.push_back(std::byte(c));
 d.push_back(std::byte(0));d.push_back(std::byte(0));Word(d,3,be);
 Add(d,"First_blinn",be,0xffffffffu);
 Add(d,"Second_Studio",be,0xfffffff8u);
 Add(d,"Other_blinn",be,7u);
 d.insert(d.end(),20,std::byte(0));return d;
}
int main() {
 for(bool be:{false,true}) {
  auto d=Model(be);
  const auto parsed=ReadAllSurfaceHeaders(d);
  assert(parsed.surfaces.size()==3 && parsed.original_model_name=="hulk_mesh");
  assert(parsed.surfaces[0].material_name=="First_blinn");
  assert(parsed.surfaces[1].material_name=="Second_Studio");
  assert(parsed.surfaces[2].material_flags==7u);
  assert(parsed.surfaces[0].ordinal==0 && parsed.surfaces[2].ordinal==2);
  assert(parsed.surfaces[2].name_offset>parsed.surfaces[1].header_end_offset);
  assert(parsed.surfaces[0].attribute_b==2u);
  d[0]=std::byte(0x40);
  bool rejected=false;
  try{(void)ReadAllSurfaceHeaders(d);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
  d=Model(be);d.resize(35);rejected=false;
  try{(void)ReadAllSurfaceHeaders(d);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
 }
}
