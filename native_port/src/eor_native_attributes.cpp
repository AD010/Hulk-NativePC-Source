#include "eor_native_attributes.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace hulk::eor::model {
namespace {
uint32_t ComponentBytes(AttributeEncoding e) {
  switch(e) {
    case AttributeEncoding::BigFloat32:return 4;
    case AttributeEncoding::BigFloat16:
    case AttributeEncoding::BigUNorm16:
    case AttributeEncoding::BigSNorm16:
    case AttributeEncoding::BigUInt16:return 2;
    case AttributeEncoding::BigArgb8:
    case AttributeEncoding::UNorm8:
    case AttributeEncoding::SNorm8:
    case AttributeEncoding::UInt8:return 1;
  }
  throw std::invalid_argument("Unknown verified EoR vertex attribute encoding");
}
uint32_t Read16(const std::byte* p) noexcept {
  return (uint32_t(std::to_integer<uint8_t>(p[0]))<<8) |
          uint32_t(std::to_integer<uint8_t>(p[1]));
}
uint32_t Read32(const std::byte* p) noexcept {
  return (Read16(p)<<16)|Read16(p+2);
}
float HalfToFloat(uint32_t raw) noexcept {
  const bool negative=(raw&0x8000)!=0;
  const uint32_t exponent=(raw>>10)&31;
  const uint32_t mantissa=raw&1023;
  float v=0;
  if(exponent==0) v=std::ldexp(float(mantissa),-24);
  else if(exponent==31) return std::numeric_limits<float>::quiet_NaN();
  else v=std::ldexp(float(1024+mantissa),int(exponent)-25);
  return negative?-v:v;
}
float Convert(const std::byte* p, AttributeEncoding e) {
  switch(e) {
    case AttributeEncoding::BigFloat32:return std::bit_cast<float>(Read32(p));
    case AttributeEncoding::BigFloat16:return HalfToFloat(Read16(p));
    case AttributeEncoding::BigUNorm16:return float(Read16(p))/65535.f;
    case AttributeEncoding::BigSNorm16: {
      const int32_t raw=int32_t(Read16(p));
      const int32_t signedValue=(raw&0x8000)?raw-0x10000:raw;
      return std::max(-1.f,float(signedValue)/32767.f);
    }
    case AttributeEncoding::BigArgb8:
    case AttributeEncoding::UNorm8:return float(std::to_integer<uint8_t>(*p))/255.f;
    case AttributeEncoding::SNorm8: {
      const int32_t raw=int32_t(std::to_integer<uint8_t>(*p));
      const int32_t signedValue=(raw&0x80)?raw-0x100:raw;
      return std::max(-1.f,float(signedValue)/127.f);
    }
    case AttributeEncoding::UInt8:return float(std::to_integer<uint8_t>(*p));
    case AttributeEncoding::BigUInt16:return float(Read16(p));
  }
  throw std::invalid_argument("Unknown verified EoR vertex attribute encoding");
}
}
std::vector<VerifiedAttributeLayout> DecodeOriginalXboxAttributeLayouts(
    const NativeIndexedMesh& mesh) {
  if(mesh.original_attribute_count<2 ||
     mesh.original_attribute_count!=mesh.original_attribute_declarations.size() ||
     mesh.original_vertex_stride<12 || mesh.original_vertex_stride>192 ||
     mesh.original_vertex_stride%4)
    throw std::invalid_argument("Original Xbox mesh attribute table incomplete");
  const auto& declarations=mesh.original_attribute_declarations;
  const auto first=declarations.front();
  const auto byte=[](std::byte item) noexcept {
    return std::to_integer<uint8_t>(item);
  };
  // Original EoR source header's first entry is a 12-byte float XYZ,
  // followed by zero or more packed XDK data attributes and one sentinel.
  if(byte(first[0])!=2 || byte(first[1])!=0 ||
     byte(first[2])!=0 || byte(first[3])!=12)
    throw std::invalid_argument("Original Xbox mesh position layout changed");
  std::vector<VerifiedAttributeLayout> result;
  result.reserve(declarations.size()-2u);
  uint32_t offset=12;
  std::array<uint8_t,256> used_texcoords{};
  bool normal=false,tangent=false,color=false,weights=false,indices=false;
  for(size_t i=1;i<declarations.size();++i) {
    const auto& entry=declarations[i];
    const uint8_t type=byte(entry[0]),usage=byte(entry[1]),
                  usage_index=byte(entry[2]),end=byte(entry[3]);
    if(i+1u==declarations.size()) {
      // The final original XDK terminator stores its semantic
      // (texcoord, next usage index) but no source vertex bytes.
      if(type!=0 || usage!=5 || end!=0 ||
         end>mesh.original_vertex_stride)
        throw std::invalid_argument("Original Xbox vertex terminator changed");
      break;
    }
    VerifiedAttributeLayout layout{};
    layout.original_byte_offset=offset;
    if(type==0x10u && usage==3u && usage_index==0u && !normal) {
      normal=true;layout.components=4;layout.encoding=AttributeEncoding::SNorm8;
    } else if(type==0x10u && usage==6u && usage_index==0u && !tangent) {
      tangent=true;layout.components=4;layout.encoding=AttributeEncoding::SNorm8;
    } else if(type==0x2eu && usage==5u && !used_texcoords[usage_index]) {
      used_texcoords[usage_index]=1;
      layout.components=4;layout.encoding=AttributeEncoding::BigFloat16;
    } else if(type==0x10u && usage==10u && usage_index==0u && !color) {
      color=true;layout.components=4;layout.encoding=AttributeEncoding::BigArgb8;
    } else if(type==0x10u && usage==1u && usage_index==0u && !weights) {
      weights=true;layout.components=4;layout.encoding=AttributeEncoding::UNorm8;
    } else if(type==0x11u && usage==2u && usage_index==0u && !indices) {
      indices=true;layout.components=4;layout.encoding=AttributeEncoding::UInt8;
    } else {
      throw std::invalid_argument("Unsupported or ambiguous original Xbox attribute");
    }
    const uint32_t encoded_bytes=layout.encoding==AttributeEncoding::BigFloat16 ? 8u:4u;
    if(uint32_t(end)!=offset+encoded_bytes || end>mesh.original_vertex_stride)
      throw std::invalid_argument("Original Xbox declaration byte ends mismatch mesh stride");
    offset=end;
    result.push_back(layout);
  }
  return result;
}
std::vector<HostAttribute> DecodeVerifiedAttribute(
    const NativeIndexedMesh& mesh, VerifiedAttributeLayout layout) {
  const uint32_t stride=mesh.original_vertex_stride;
  const uint32_t bytes=ComponentBytes(layout.encoding);
  if(stride<12 || stride>192 || stride%4 ||
     !layout.components || layout.components>4 ||
     (layout.encoding==AttributeEncoding::BigArgb8 && layout.components!=4) ||
     layout.original_byte_offset>stride ||
     uint32_t(layout.components)*bytes>stride-layout.original_byte_offset ||
     mesh.positions.size()>SIZE_MAX/stride ||
     mesh.original_vertex_stream.size()!=mesh.positions.size()*stride)
    throw std::invalid_argument("Unverified/out-of-range EoR vertex attribute layout");
  std::vector<HostAttribute> out;
  out.reserve(mesh.positions.size());
  for(size_t i=0;i<mesh.positions.size();++i) {
    HostAttribute value{};
    const std::byte* p=mesh.original_vertex_stream.data()+i*stride+layout.original_byte_offset;
    for(uint32_t c=0;c<layout.components;++c) {
      // A big-endian D3DCOLOR word is A,R,G,B in memory. The XDK
      // declaration fetch presents R,G,B,A to the shader.
      const uint32_t component=layout.encoding==AttributeEncoding::BigArgb8 ? (c+1u)%4u : c;
      const float v=Convert(p+component*bytes,layout.encoding);
      if(!std::isfinite(v))
        throw std::invalid_argument("Nonfinite original EoR vertex attribute");
      value[c]=v;
    }
    out.push_back(value);
  }
  return out;
}
}
