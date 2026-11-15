// Native original Havok packfile envelope from EoR Havoks / HavokMopps.
// Does NOT deserialize physics shape graphs or modify the game's solver.
#pragma once
#include "eor_archive.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>
namespace hulk::eor::havok {
enum class Platform { PC, Xbox360 };
struct Section {
  uint32_t absolute_start = 0, local_fixups = 0, global_fixups = 0;
  uint32_t virtual_fixups = 0, exports = 0, imports = 0, end = 0;
};
struct Packfile {
  uint32_t indexed_hash = 0, file_version = 0, sections = 0;
  size_t header_offset = 0, payload_bytes = 0;
  Platform platform = Platform::PC;
  std::array<Section,3> section_table{};
};
Packfile Inspect(std::span<const std::byte> bytes,
                 uint32_t index_hash, Platform platform);
enum class FixupKind { Local, Global, VirtualClass };
struct Fixup {
  FixupKind kind = FixupKind::Local;
  uint32_t from_section = 0, from_offset = 0;
  uint32_t to_section = 0, to_offset = 0;
};
// Section-relative original Havok relocation edges, never host pointers.
std::vector<Fixup> DecodeFixups(std::span<const std::byte> bytes,
                                uint32_t index_hash, Platform platform);
Packfile ReadIndexed(const Index& index, const std::filesystem::path& archive,
                     std::string_view category, uint32_t hash,
                     size_t maximum_size = 4u << 20);
} // namespace hulk::eor::havok
