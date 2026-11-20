#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hulk::eor::gpu {

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
  std::array<uint32_t, 16> next_offset{};
  std::array<uint32_t, 16> highest_end{};
  std::array<uint16_t, 16> occupied{};
  std::array<bool, 16> used{};
  std::array<bool, 16> saw_auto_stride{};
  for (const auto& attribute : attributes) {
    if (!attribute.float32 || !attribute.per_vertex ||
        attribute.slot >= used.size() || attribute.components == 0 ||
        attribute.components > 4)
      return false;
    const uint32_t slot = attribute.slot;
    const uint32_t width = slot == 0 ? 12u : 16u;
    const uint32_t size = attribute.components * 4u;
    const uint32_t offset = attribute.offset == auto_offset ?
        next_offset[slot] : attribute.offset;
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
    used[slot] = true;
    saw_auto_stride[slot] |= attribute.stride == auto_stride;
    next_offset[slot] = offset + size;
    if (next_offset[slot] > highest_end[slot])
      highest_end[slot] = next_offset[slot];
  }
  if (!used[0]) return false;
  bool seen_gap = false;
  for (size_t slot = 0; slot < used.size(); ++slot) {
    if (!used[slot]) {
      seen_gap = true;
      continue;
    }
    if (seen_gap || (saw_auto_stride[slot] &&
                     highest_end[slot] != (slot == 0 ? 12u : 16u)))
      return false;
  }
  return true;
}

} // namespace hulk::eor::gpu
