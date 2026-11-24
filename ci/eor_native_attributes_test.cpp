#include "../native_port/src/eor_native_attributes.h"
#include <cassert>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::model;
namespace {
void U16(std::vector<std::byte>& b,uint16_t x) {b.push_back(std::byte(x>>8));b.push_back(std::byte(x));}
void U32(std::vector<std::byte>& b,uint32_t x) {U16(b,uint16_t(x>>16));U16(b,uint16_t(x));}
void F32(std::vector<std::byte>& b,float x) {U32(b,std::bit_cast<uint32_t>(x));}
template<class F> void Reject(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}assert(caught);}
NativeIndexedMesh Mesh() {
 NativeIndexedMesh m;
 m.original_vertex_stride=28;
 m.positions={{0,0,0},{1,0,0},{0,1,0}};
 m.triangle_list_indices={0,1,2};
 const float uv[3][2]={{0.f,1.f},{0.25f,0.5f},{1.f,0.f}};
 for(const auto& xy:uv) {
   F32(m.original_vertex_stream,0);F32(m.original_vertex_stream,0);F32(m.original_vertex_stream,0);
   F32(m.original_vertex_stream,xy[0]);F32(m.original_vertex_stream,xy[1]);
   U16(m.original_vertex_stream,0);U16(m.original_vertex_stream,0);U16(m.original_vertex_stream,0x7fff);U16(m.original_vertex_stream,0);
 }
 return m;
}
}
int main(){
  {
    NativeIndexedMesh mesh;
    mesh.positions.resize(1);mesh.original_vertex_stride=4;
    auto word=[&](uint32_t v){
      mesh.original_vertex_stream.clear();U32(mesh.original_vertex_stream,v);
    };
    word(0x80007fffu);
    auto shorts=DecodeVerifiedAttribute(mesh,{0,2,AttributeEncoding::BigSInt16});
    assert(shorts.size()==1 && shorts[0][0]==-32768.f && shorts[0][1]==32767.f);
    word(1023u | (512u<<10) | (511u<<20));
    auto u=DecodeVerifiedAttribute(mesh,{0,3,AttributeEncoding::BigUInt10_10_10});
    assert(u[0][0]==1023.f && u[0][1]==512.f && u[0][2]==511.f);
    auto sn=DecodeVerifiedAttribute(mesh,{0,3,AttributeEncoding::BigSNorm10_10_10});
    assert(sn[0][0]==-1.f/511.f && sn[0][1]==-1.f && sn[0][2]==1.f);
  }
 const auto m=Mesh();
 auto uv=DecodeVerifiedAttribute(m,{12,2,AttributeEncoding::BigFloat32});
 assert(uv.size()==3 && uv[1][0]==0.25f && uv[1][1]==0.5f && uv[1][2]==0);
 auto n=DecodeVerifiedAttribute(m,{20,3,AttributeEncoding::BigSNorm16});
 assert(n.size()==3 && n[0][0]==0 && n[0][2]==1.0f);
 auto byte=DecodeVerifiedAttribute(m,{25,1,AttributeEncoding::UInt8});
 assert(byte.size()==3 && byte[0][0]==255.f);
 auto half=m;
 // +1.0 (0x3c00), -1.0 (0xbc00) in BE IEEE-754 binary16.
 half.original_vertex_stream[12]=std::byte(0x3c);half.original_vertex_stream[13]=std::byte(0x00);
 half.original_vertex_stream[14]=std::byte(0xbc);half.original_vertex_stream[15]=std::byte(0x00);
 auto values=DecodeVerifiedAttribute(half,{12,2,AttributeEncoding::BigFloat16});
 assert(values[0][0]==1 && values[0][1]==-1);
 half.original_vertex_stream[12]=std::byte(0x7c);half.original_vertex_stream[13]=std::byte(0x00);
 Reject([&]{(void)DecodeVerifiedAttribute(half,{12,2,AttributeEncoding::BigFloat16});});
 Reject([&]{(void)DecodeVerifiedAttribute(m,{26,2,AttributeEncoding::BigFloat32});});
 Reject([&]{(void)DecodeVerifiedAttribute(m,{12,0,AttributeEncoding::BigFloat32});});
 Reject([&]{(void)DecodeVerifiedAttribute(m,{12,2,static_cast<AttributeEncoding>(255)});});
 auto broken=m;broken.original_vertex_stream.pop_back();
 Reject([&]{(void)DecodeVerifiedAttribute(broken,{12,2,AttributeEncoding::BigFloat32});});
}
