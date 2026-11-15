#include "../native_port/src/eor_native_mesh_geometry.h"
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor::model;
static void U32(std::vector<std::byte>& b,uint32_t x) {
 for(int i=3;i>=0;--i)b.push_back(std::byte(x>>(8*i)));
}
static void U16(std::vector<std::byte>& b,uint16_t x) {
 b.push_back(std::byte(x>>8));b.push_back(std::byte(x));
}
static std::vector<std::byte> TestModel() {
 std::vector<std::byte> b{std::byte(0),std::byte(1),std::byte(0),
                          std::byte(0),std::byte(0),std::byte(0)};
 for(char c:std::string("native_geometry"))b.push_back(std::byte(c));
 b.push_back(std::byte(0));b.push_back(std::byte(0));U32(b,1);
 for(char c:std::string("Ground"))b.push_back(std::byte(c));
 b.push_back(std::byte(0));U32(b,0);U32(b,0);U32(b,0xffffffffu);
 b.insert(b.end(),90,std::byte(0));
 b.insert(b.end(),{std::byte(1),std::byte(0),std::byte(0),std::byte(0),
                   std::byte(0),std::byte(3),std::byte(24),std::byte(0)});
 for(int i=0;i<12;++i)b.push_back(std::byte(0));
 b.insert(b.end(),{std::byte(0),std::byte(0),std::byte(6)});
 U32(b,4);
 for(uint16_t x:{0,1,2,0,2,3})U16(b,x);
 const float verts[4][3]={{-1,0,0},{1,0,0},{1,1,0},{-1,1,0}};
 for(const auto& p:verts) {
  for(float x:p)U32(b,std::bit_cast<uint32_t>(x));
  b.insert(b.end(),12,std::byte(0));
 }
 return b;
}
int main() {
 auto model=TestModel();
 const auto meshes=DecodeNativeXboxIndexedMeshes(model);
 assert(meshes.size()==1 && meshes[0].positions.size()==4);
 assert(meshes[0].triangle_list_indices.size()==6 &&
        meshes[0].triangle_list_indices[4]==2);
 assert(meshes[0].positions[0].x==-1.f &&
        meshes[0].positions[2].y==1.f);
 assert(meshes[0].original_vertex_stride==24 &&
        meshes[0].original_attribute_count==3 &&
        meshes[0].original_vertex_stream.size()==96);
 auto bad=model;
 bad[0]=std::byte(1);bad[1]=std::byte(0);
 bool rejected=false;
 try{(void)DecodeNativeXboxIndexedMeshes(bad);}
 catch(const std::invalid_argument&){rejected=true;}
 assert(rejected);
 bad=model;
 const auto indexOffset=meshes[0].source_indices_offset;
 bad[indexOffset]=std::byte(0xff);bad[indexOffset+1]=std::byte(0xff);
 assert(DecodeNativeXboxIndexedMeshes(bad).empty());
 bad=model;bad.resize(meshes[0].source_vertices_offset+8);
 assert(DecodeNativeXboxIndexedMeshes(bad).empty());
}
