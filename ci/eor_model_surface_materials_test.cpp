#include "../native_port/src/eor_model_surface_materials.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor;
static void Word(std::vector<std::byte>& d,uint32_t n,bool be) {
 for(int i=0;i<4;++i)d.push_back(std::byte(n>>(8*(be?3-i:i))));
}
static void Set(std::vector<std::byte>& d,size_t at,uint32_t n,bool be) {
 for(int i=0;i<4;++i)d[at+i]=std::byte(n>>(8*(be?3-i:i)));
}
static std::vector<std::byte> IndexBytes(bool be) {
 std::vector<std::byte>d;Word(d,2,be);
 Word(d,0,be);Word(d,0,be);Word(d,0,be);
 const auto name=uint32_t(d.size());
 for(char c:std::string("Textures"))d.push_back(std::byte(c));
 d.push_back(std::byte(0));
 const auto table=uint32_t(d.size());Word(d,2,be);
 const auto a=ResourceHash("GroundTexture"),b=ResourceHash("NormalTexture");
 if(a<b){Word(d,a,be);Word(d,b,be);}else{Word(d,b,be);Word(d,a,be);}
 Word(d,512,be);Word(d,64,be);Word(d,0,be);
 Word(d,576,be);Word(d,128,be);Word(d,0,be);
 const auto end=uint32_t(d.size());Word(d,1000,be);
 Set(d,4,name,be);Set(d,8,table,be);Set(d,12,end,be);
 return d;
}
static std::vector<std::byte> ModelBytes(bool be) {
 std::vector<std::byte>d{std::byte(be?0:1),std::byte(be?1:0)};
 d.insert(d.end(),4,std::byte(0));
 for(char c:std::string("hulk_ground"))d.push_back(std::byte(c));
 d.push_back(std::byte(0));d.push_back(std::byte(0));Word(d,2,be);
 for(unsigned i=0;i<2;++i) {
  for(char c:std::string(i==0?"Roof":"Sidewalk"))d.push_back(std::byte(c));
  d.push_back(std::byte(0));Word(d,0,be);Word(d,0,be);Word(d,0xffffffffu,be);
  const auto base=d.size()-12;
  d.resize(base+144);
  Set(d,base+(be?56u:48u),ResourceHash("GroundTexture"),be);
  if(be)Set(d,base+60,ResourceHash("NormalTexture"),be);
 }
 return d;
}
int main() {
 for(bool be:{false,true}) {
  auto original=ModelBytes(be);
  auto indexed=Index::Decode(IndexBytes(be));
  auto all=model::AllSurfaceMaterialCandidates(original,indexed);
  assert(all.size()==2);
  for(size_t i=0;i<all.size();++i) {
   assert(all[i].ordinal==i);
   assert(all[i].original_material_name==(i==0?"Roof":"Sidewalk"));
   assert(all[i].indexed_texture_references.size()==(be?2u:1u));
   assert(all[i].indexed_texture_references[0].original_texture_hash==
          ResourceHash("GroundTexture"));
  }
  bool rejected=false;
  try {auto wrong=Index::Decode(IndexBytes(!be));
       (void)model::AllSurfaceMaterialCandidates(original,wrong);}
  catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
  original[0]=std::byte(0xff);rejected=false;
  try{(void)model::AllSurfaceMaterialCandidates(original,indexed);}
  catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
 }
}
