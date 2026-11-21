#pragma once
// Decode actual user-owned original PC 2008 Hulk EoR UI texture records.
// Never treat Xbox LFXT bytes as a PC DDS or bundle original game assets.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace hulk::eor::ui {
struct OriginalPcUiImage {
  uint32_t width=0,height=0;
  std::vector<uint8_t> rgba;
};
OriginalPcUiImage DecodePcTxflUi(
    std::span<const std::byte> original,std::string_view original_name);
OriginalPcUiImage LoadPcMenuArt(
    const std::filesystem::path& index_pc,
    const std::filesystem::path& textures_pc,
    std::string_view original_name);
}
