// Original 2008 Edge of Reality font_clean record metadata, not a font renderer.
// PC and Xbox profiles use opposite endianness; glyph bytes never cross them.
#pragma once
#include "eor_archive.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hulk::eor {
struct FontProfile {
  uint32_t width = 0, height = 0;
  float profile_parameter = 0.0f;  // Original third field; no guessed semantics.
  uint32_t data_offset = 0, data_length = 0;
};
struct FontPack {
  std::vector<FontProfile> profiles;
};

// Read the original 'font_clean' resource payload returned by
// OriginalUiAssets::Read. No archive prefix, host texture conversion or
// original proprietary resource bytes stored in this project.
FontPack DecodeFontPack(std::span<const std::byte> data, ByteOrder order);
}  // namespace hulk::eor
