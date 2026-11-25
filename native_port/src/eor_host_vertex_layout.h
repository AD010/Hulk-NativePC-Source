#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hulk::eor::gpu {

// Immutable upload kinds are independent from Vulkan's input format.
// Normal/tangent float4 bytes are deliberately interpreted as uint4 in
// the scalar shader (asfloat). Kind 4 is ONLY numerical bone indices.
inline constexpr uint32_t OriginalVertexStorageKind(
    uint32_t slot,uint32_t location,bool uint_bits,bool runtime,bool font) noexcept {
  if(font)return uint_bits?UINT32_MAX:3u;
  if(!runtime && slot==0u)return uint_bits||location!=0u?UINT32_MAX:0u;
  if(uint_bits) {
    if(location==9u)return 4u;
    if(location==1u || location==2u)return 3u;
    return UINT32_MAX;
  }
  return location==9u?UINT32_MAX:3u;
}
struct HostVertexAttribute {
  uint32_t slot = 0;
  uint32_t components = 0;
  uint32_t offset = 0;
  uint32_t stride = 0;
  bool float32 = false;
  bool per_vertex = false;
};

// Position stream 0 consists of precisely one decoded float3 per vertex.
// Other original, independently decoded host attribute streams each contain
// one float4 per vertex. Multiple shader inputs may split an attribute stream,
// provided their byte ranges never alias and the complete layout is in bounds.
// The two auto markers are supplied by the caller's graphics API so this
// validator is independent of Diligent and can be tested without a GPU SDK.
inline bool ValidateHostVertexLayout(std::span<const HostVertexAttribute> attributes,
                                     uint32_t auto_offset,
                                     uint32_t auto_stride) noexcept {
  if (attributes.empty() || attributes.size() > 32)
    return false;
  std::array<uint32_t, 16> highest_end{};
  std::array<uint16_t, 16> occupied{};
  std::array<bool, 16> explicit_stride{};
  for (const auto& attribute : attributes) {
    if (!attribute.float32 || !attribute.per_vertex ||
        attribute.slot >= occupied.size() || attribute.components == 0 ||
        attribute.components > 4)
      return false;
    const uint32_t slot = attribute.slot;
    const uint32_t width = slot == 0 ? 12u : 16u;
    const uint32_t size = attribute.components * 4u;
    const uint32_t offset = attribute.offset == auto_offset ?
        highest_end[slot] : attribute.offset;
    if (offset > width || offset % 4u || size > width - offset ||
        (attribute.stride != width && attribute.stride != auto_stride))
      return false;
    if (slot == 0 && (attribute.components != 3 || offset != 0))
      return false;
    const uint16_t byte_mask = static_cast<uint16_t>(
        ((uint32_t{1} << size) - 1u) << offset);
    if (occupied[slot] & byte_mask)
      return false;
    occupied[slot] = static_cast<uint16_t>(occupied[slot] | byte_mask);
    explicit_stride[slot] |= attribute.stride != auto_stride;
    // Diligent advances AUTO_OFFSET from the largest previous byte end,
    // including when explicit elements are declared out of byte order.
    if (offset + size > highest_end[slot])
      highest_end[slot] = offset + size;
  }
  if (!occupied[0]) return false;
  for (size_t slot = 0; slot < occupied.size(); ++slot) {
    // One explicit stride applies to every element of this binding.
    if (occupied[slot] && !explicit_stride[slot] &&
        highest_end[slot] != (slot == 0 ? 12u : 16u))
      return false;
  }
  return true;
}

} // namespace hulk::eor::gpu
