#include "../native_port/src/eor_original_texture_assets.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor;
namespace {
void U32(std::vector<std::byte>& b,uint32_t n,bool big) {
  for(int i=0;i<4;++i)b.push_back(std::byte(n>>(big?(24-8*i):(8*i))));
}
void Put32(std::vector<std::byte>& b,size_t p,uint32_t n,bool big) {
  for(int i=0;i<4;++i)b[p+i]=std::byte(n>>(big?(24-8*i):(8*i)));
}
void Save(const std::filesystem::path& p,const std::vector<std::byte>& b) {
  std::ofstream f(p,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
}
std::vector<std::byte> IndexFor(bool big,uint32_t x,uint32_t y) {
  std::vector<std::byte> b;U32(b,2,big);
  for(int i=0;i<3;++i)U32(b,0,big);
  const uint32_t name=uint32_t(b.size());
  for(char c:std::string("Textures"))b.push_back(std::byte(c));
  b.push_back(std::byte(0));
  const uint32_t tab=uint32_t(b.size());
  U32(b,2,big);U32(b,x,big);U32(b,y,big);
  U32(b,4,big);U32(b,3,big);U32(b,0,big);
  U32(b,7,big);U32(b,4,big);U32(b,0,big);
  const uint32_t end=uint32_t(b.size());U32(b,0,big);
  Put32(b,4,name,big);Put32(b,8,tab,big);Put32(b,12,end,big);
  return b;
}
template<class F> void Reject(F f){bool yes=false;try{f();}catch(const std::invalid_argument&){yes=true;}assert(yes);}
model::SurfaceMaterials Surface(uint32_t x,uint32_t y) {
  model::SurfaceMaterials s;
  s.ordinal=0;s.original_material_name="brick";
  s.indexed_texture_references.push_back({x,56,{x,4,3,0}});
  s.indexed_texture_references.push_back({y,60,{y,7,4,0}});
  s.indexed_texture_references.push_back({x,64,{x,4,3,0}});
  return s;
}
}
int main(){
  const uint32_t x=0x12341234u,y=0x12341235u;
  const auto folder=std::filesystem::temp_directory_path();
  const auto idx=folder/"eor_textures_opaque_test.ind";
  const auto arc=folder/"eor_textures_opaque_test.arc";
  // Two opaque bytestrings. There is intentionally NO format/mip guess.
  Save(arc,{std::byte(0),std::byte(0),std::byte(0),std::byte(0),
            std::byte(0xab),std::byte(0xcd),std::byte(0xef),
            std::byte(0x7a),std::byte(0x7b),std::byte(0x7c),std::byte(0x7d)});
  Save(idx,IndexFor(true,x,y));
  auto source=textures::OriginalXboxTextureAssets::Open(idx,arc);
  const auto original=source.LoadHash(x);
  assert(original && original->original_bytes.size()==3);
  assert(!original->pinned_usa_index); // synthetic or old regional index is never USA
  assert(original->original_bytes[0]==std::byte(0xab));
  assert(!source.LoadHash(0xfedcba98u));
  auto s=Surface(x,y);
  const auto batch=source.LoadSurfaceCandidates(std::span(&s,1));
  assert(batch.surfaces.size()==1);
  assert(batch.surfaces[0].candidate_slots.size()==3);
  assert(batch.surfaces[0].candidate_slots[2].offset_from_surface_data==64);
  assert(batch.unique_textures.size()==2);
  assert(batch.unique_textures[1].hash==y);
  assert(batch.unique_textures[1].original_bytes[3]==std::byte(0x7d));
  auto invalid=s;invalid.indexed_texture_references[1].original_resource.size=999;
  Reject([&]{(void)source.LoadSurfaceCandidates(std::span(&invalid,1));});
  invalid=s;invalid.ordinal=1;
  Reject([&]{(void)source.LoadSurfaceCandidates(std::span(&invalid,1));});
  invalid=s;invalid.indexed_texture_references[0].original_texture_hash=0xfffu;
  Reject([&]{(void)source.LoadSurfaceCandidates(std::span(&invalid,1));});
  Save(idx,IndexFor(false,x,y));
  Reject([&]{(void)textures::OriginalXboxTextureAssets::Open(idx,arc);});
  Save(idx,IndexFor(true,x,y));
  Save(arc,{std::byte(0),std::byte(0),std::byte(0),std::byte(0),std::byte(0xab)});
  bool truncated=false;
  try{(void)source.LoadSurfaceCandidates(std::span(&s,1));}catch(const std::runtime_error&){truncated=true;}
  assert(truncated);
  std::filesystem::remove(idx);std::filesystem::remove(arc);
}
