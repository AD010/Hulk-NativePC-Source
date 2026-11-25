#pragma once
// Shared primitive readers for original PC/Xbox EoR model records.
// Callers must establish the bounds of the containing source field first.
// No unaligned pointer casts or dependence on the host's byte order.
#include "eor_model_resource.h"
#include <array>

namespace hulk::eor::model::binary {
inline uint8_t B(std::span<const std::byte> data,size_t at) noexcept {
  return std::to_integer<uint8_t>(data[at]);
}
inline uint32_t U32(std::span<const std::byte> data,size_t at,Platform p) noexcept {
  const uint32_t a=B(data,at),b=B(data,at+1),c=B(data,at+2),d=B(data,at+3);
  return p==Platform::Xbox360 ? (a<<24)|(b<<16)|(c<<8)|d :
                               a|(b<<8)|(c<<16)|(d<<24);
}
inline uint32_t BE32(std::span<const std::byte> data,size_t at) noexcept {
  return U32(data,at,Platform::Xbox360);
}
// These are the original, observed material-relative offsets, not
// generalized shader sampler numbers. The caller owns the material bounds.
inline constexpr std::array<uint32_t,7> kXboxTextureSlots{56,60,64,68,72,76,80};
inline constexpr std::array<uint32_t,2> kPcTextureSlots{48,52};
inline constexpr std::span<const uint32_t> TextureSlots(Platform p) noexcept {
  if(p==Platform::Xbox360)return kXboxTextureSlots;
  return kPcTextureSlots;
}
} // namespace hulk::eor::model::binary
