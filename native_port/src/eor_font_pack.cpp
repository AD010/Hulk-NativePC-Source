#include "eor_font_pack.h"
#include <bit>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace hulk::eor {
namespace {
uint32_t Read32(std::span<const std::byte> data, size_t offset, ByteOrder order) {
  if (offset > data.size() || data.size() - offset < 4)
    throw std::invalid_argument("Truncated EoR font metadata");
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) {
    const uint32_t octet = std::to_integer<uint8_t>(data[offset + i]);
    if (order == ByteOrder::Big) value = (value << 8) | octet;
    else value |= octet << (8u * i);
  }
  return value;
}
}  // namespace

FontPack DecodeFontPack(std::span<const std::byte> data, ByteOrder order) {
  // In original archives the first half of these blocks contains display
  // metadata; the second half contains separate, unconverted font payloads.
  if (data.size() < 8 || data.size() > (4u << 20))
    throw std::invalid_argument("Invalid original EoR font size");
  const uint32_t chunks = Read32(data, 0, order);
  if (!chunks || (chunks & 1u) || chunks > 64)
    throw std::invalid_argument("Invalid original EoR font chunk count");
  const uint32_t n = chunks / 2u;
  const uint64_t header_length = 4ull + (uint64_t(chunks) + 1ull) * 4ull;
  if (header_length > data.size())
    throw std::invalid_argument("Truncated original EoR font offsets");
  std::vector<uint32_t> offsets;
  offsets.reserve(size_t(chunks) + 1u);
  for (uint32_t i = 0; i <= chunks; ++i)
    offsets.push_back(Read32(data, 4ull + uint64_t(i) * 4ull, order));
  if (offsets.front() < header_length || offsets.back() != data.size())
    throw std::invalid_argument("Invalid EoR font payload boundaries");
  for (size_t i = 1; i < offsets.size(); ++i)
    if (offsets[i] <= offsets[i - 1] || offsets[i] > data.size())
      throw std::invalid_argument("Overlapping or invalid EoR font offsets");

  FontPack out;
  out.profiles.reserve(n);
  constexpr std::string_view name = "font_clean";
  for (uint32_t i = 0; i < n; ++i) {
    if (offsets[i + 1] - offsets[i] != 12u)
      throw std::invalid_argument("Unknown EoR font profile header");
    const uint32_t w = Read32(data, offsets[i], order);
    const uint32_t h = Read32(data, offsets[i] + 4u, order);
    const float value = std::bit_cast<float>(Read32(data, offsets[i] + 8u, order));
    if (w < 64u || h < 64u || w > 16384u || h > 16384u ||
        !std::isfinite(value) || value <= 0.0f || value > 8.0f)
      throw std::invalid_argument("Invalid original EoR font display profile");
    const uint32_t begin = offsets[i + n];
    const uint32_t end = offsets[i + n + 1u];
    if (end - begin < name.size() + 1u)
      throw std::invalid_argument("Truncated EoR embedded font payload");
    for (size_t j = 0; j < name.size(); ++j) {
      if (std::to_integer<uint8_t>(data[begin + j]) != uint8_t(name[j]))
        throw std::invalid_argument("Unexpected original EoR font identifier");
    }
    if (data[begin + name.size()] != std::byte{0})
      throw std::invalid_argument("Unterminated EoR embedded font identifier");
    out.profiles.push_back(FontProfile{w, h, value, begin, end - begin});
  }
  return out;
}
}  // namespace hulk::eor
