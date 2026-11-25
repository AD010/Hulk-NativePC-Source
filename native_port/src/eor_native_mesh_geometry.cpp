#include "eor_native_mesh_geometry.h"
#include "eor_model_binary.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace hulk::eor::model {
namespace {
using binary::B;
using binary::BE32;
uint32_t BE24(std::span<const std::byte>d,size_t i) noexcept {
 return (uint32_t(B(d,i))<<16)|(uint32_t(B(d,i+1))<<8)|B(d,i+2);
}
uint32_t BE16(std::span<const std::byte>d,size_t i) noexcept {
 return (uint32_t(B(d,i))<<8)|B(d,i+1);
}
float BEFloat(std::span<const std::byte>d,size_t i) noexcept {
 return std::bit_cast<float>(BE32(d,i));
}
} // namespace
std::vector<NativeIndexedMesh> DecodeNativeXboxIndexedMeshes(
 std::span<const std::byte> data) {
 const auto id=Inspect(data);
 if(id.platform!=Platform::Xbox360)
  throw std::invalid_argument("PC D3D9 model uses a different geometry format");
 std::vector<NativeIndexedMesh> result;
 // Original Xbox EoR layout:
 // 01 00 00 00 00, attribute count, stride, reserved, 4*N vertex decl;
 // BE24 triangle index count, BE32 vertex count, BE16 indices;
 // interleaved source vertex elements with BE32 float XYZ at offsets 0,4,8.
 for(size_t at=0;at+15<data.size();) {
  if(B(data,at)!=1 || B(data,at+1)!=0 || B(data,at+2)!=0 ||
     B(data,at+3)!=0 || B(data,at+4)!=0) {++at;continue;}
  const uint32_t attributes=B(data,at+5),stride=B(data,at+6);
  if(attributes<1 || attributes>16 || stride<12 || stride>192 ||
     stride%4 || B(data,at+7)!=0) {++at;continue;}
  const size_t counts=at+8+size_t(attributes)*4;
  if(counts>data.size() || data.size()-counts<7) {++at;continue;}
  const uint32_t indexCount=BE24(data,counts),vertexCount=BE32(data,counts+3);
  if(indexCount<3 || indexCount>65535 || indexCount%3 ||
     vertexCount<3 || vertexCount>65535 ||
     indexCount>vertexCount*64u || indexCount<vertexCount/2u) {++at;continue;}
  const size_t ib=counts+7;
  if(indexCount>(data.size()-ib)/2) {++at;continue;}
  const size_t vb=ib+2*size_t(indexCount);
  if(vertexCount>(data.size()-vb)/stride) {++at;continue;}
  const size_t end=vb+size_t(vertexCount)*stride;
  NativeIndexedMesh mesh;
  mesh.source_header_offset=at;
  mesh.source_indices_offset=ib;
  mesh.source_vertices_offset=vb;
  mesh.original_vertex_stride=stride;
  mesh.original_attribute_count=attributes;
  mesh.original_attribute_declarations.reserve(attributes);
  for(size_t j=0;j<attributes;++j) {
   std::array<std::byte,4> raw{};
   for(size_t k=0;k<4;++k)raw[k]=data[at+8+4*j+k];
   mesh.original_attribute_declarations.push_back(raw);
  }
  mesh.triangle_list_indices.reserve(indexCount);
  uint32_t maximum=0;
  bool invalid=false;
  for(size_t i=0;i<indexCount;++i) {
   const uint32_t index=BE16(data,ib+i*2);
   if(index>=vertexCount) {invalid=true;break;}
   maximum=std::max(maximum,index);
   mesh.triangle_list_indices.push_back(index);
  }
  // Every validated real original chunk uses every vertex at least once.
  if(invalid || maximum!=vertexCount-1u) {++at;continue;}
  mesh.positions.reserve(vertexCount);
  for(size_t i=0;i<vertexCount;++i) {
   const size_t p=vb+i*stride;
   const float x=BEFloat(data,p),y=BEFloat(data,p+4),z=BEFloat(data,p+8);
   if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||
      std::abs(x)>1e6f||std::abs(y)>1e6f||std::abs(z)>1e6f) {
    invalid=true;break;
   }
   mesh.positions.push_back(NativePosition{x,y,z});
  }
  if(invalid) {++at;continue;}
  mesh.original_vertex_stream.assign(data.begin()+vb,data.begin()+end);
  result.push_back(std::move(mesh));
  at=end; // A decoded geometry payload is never scanned for nested headers.
 }
 return result;
}
}
