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
std::vector<HostAttribute> DecodeVerifiedAttribute(
    const NativeIndexedMesh& mesh, VerifiedAttributeLayout layout) {
  const uint32_t stride=mesh.original_vertex_stride;
  const uint32_t bytes=ComponentBytes(layout.encoding);
  if(stride<12 || stride>192 || stride%4 ||
     !layout.components || layout.components>4 ||
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
      const float v=Convert(p+c*bytes,layout.encoding);
      if(!std::isfinite(v))
        throw std::invalid_argument("Nonfinite original EoR vertex attribute");
      value[c]=v;
    }
    out.push_back(value);
  }
  return out;
}
}
