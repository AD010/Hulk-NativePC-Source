#pragma once
// READ-ONLY Hulk 2008 retail PC / Xbox save envelope validation.
// Does NOT endian-swap opaque pointers or write official PC HULK.sav.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
namespace hulk::save::format {
enum class Kind { XboxBigEndian, RetailPcLittleEndian };
struct Summary {
  Kind kind=Kind::XboxBigEndian;
  uint32_t crc32=0, length_field=0, sorted_field_count=0, zero_value_count=0;
  size_t total_bytes=0;
  bool pc_payload_conversion_ready=false;
};
Summary Inspect(std::span<const std::byte> data, Kind kind);
Summary Read(const std::filesystem::path& path, Kind kind,
             size_t maximum_bytes=16u<<20);
}
