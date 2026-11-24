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
  UNorm8, SNorm8, UInt8, BigUInt16, BigArgb8,
  BigSInt16, BigUInt10_10_10, BigSNorm10_10_10
};
struct VerifiedAttributeLayout {
  uint32_t original_byte_offset=0;
  uint8_t components=0;
  AttributeEncoding encoding=AttributeEncoding::BigFloat32;
  bool operator==(const VerifiedAttributeLayout&) const = default;
};
using HostAttribute = std::array<float,4>;
// The original Xbox MODELS.ARC already embeds its real compact attribute
// declaration in EACH mesh: [type, usage, usage_index, cumulative_byte_end].
// Convert only known source type/usage combinations and exact byte widths,
// retaining original attribute order as host Vulkan stream slots 1..N.
// This is not a shader-input-layout guess: the live source XDK declaration
// still independently verifies which slots an actual shader consumes.
std::vector<VerifiedAttributeLayout> DecodeOriginalXboxAttributeLayouts(
    const NativeIndexedMesh& source);
// Each output vector index corresponds to the decoded model vertex index.
// Missing components are zero. Rejects nonfinite values, invalid layouts,
// overflow and missing original bytes before returning any converted array.
std::vector<HostAttribute> DecodeVerifiedAttribute(
    const NativeIndexedMesh& mesh, VerifiedAttributeLayout verified_layout);
}
