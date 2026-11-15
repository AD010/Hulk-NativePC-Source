// Native original Havok packfile envelope from EoR Havoks / HavokMopps.
// Does NOT deserialize physics shape graphs or modify the game's solver.
#pragma once
#include "eor_archive.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
namespace hulk::eor::havok {
enum class Platform { PC, Xbox360 };
struct Packfile {
  uint32_t indexed_hash = 0, file_version = 0, sections = 0;
  size_t header_offset = 0, payload_bytes = 0;
  Platform platform = Platform::PC;
};
Packfile Inspect(std::span<const std::byte> bytes,
                 uint32_t index_hash, Platform platform);
Packfile ReadIndexed(const Index& index, const std::filesystem::path& archive,
                     std::string_view category, uint32_t hash,
                     size_t maximum_size = 4u << 20);
} // namespace hulk::eor::havok
