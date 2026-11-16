#pragma once
// Host conversion for an EXPLICITLY IDENTIFIED EoR Xbox 360 vertex stream.
// The meaning/offset/packing must come from validated original declarations;
// this module never guesses normals, UVs, skinning, or material passes.
#include "eor_native_mesh_geometry.h"
#include <array>
#include <cstdint>
#include <vector>
namespace hulk::eor::model {
enum class AttributeEncoding : uint8_t {
  BigFloat32, BigFloat16, BigUNorm16, BigSNorm16,
  UNorm8, SNorm8, UInt8, BigUInt16
};
struct VerifiedAttributeLayout {
  uint32_t original_byte_offset=0;
  uint8_t components=0;
  AttributeEncoding encoding=AttributeEncoding::BigFloat32;
};
using HostAttribute = std::array<float,4>;
// Each output vector index corresponds to the decoded model vertex index.
// Missing components are zero. Rejects nonfinite values, invalid layouts,
// overflow and missing original bytes before returning any converted array.
std::vector<HostAttribute> DecodeVerifiedAttribute(
    const NativeIndexedMesh& mesh, VerifiedAttributeLayout verified_layout);
}
