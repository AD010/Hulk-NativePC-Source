#pragma once
// READ-ONLY Hulk 2008 retail PC / Xbox save envelope validation.
// Does NOT endian-swap opaque pointers or write official PC HULK.sav.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
namespace hulk::save::format {
enum class Kind { XboxBigEndian, RetailPcLittleEndian };
struct Summary {
  Kind kind=Kind::XboxBigEndian;
  uint32_t crc32=0, length_field=0, sorted_field_count=0, zero_value_count=0;
  size_t total_bytes=0;
  bool pc_payload_conversion_ready=false;
  // Exact known table schema only; zero means the generic envelope reader
  // did not authenticate all five indexed regions.
  uint32_t verified_table_count=0,verified_field_count=0;
};
// The verified common table contains hashed identifiers and 32-bit
// value words. Values may encode platform-specific references. This codec
// preserves all unknown bytes and never authorizes PC/Xbox conversion.
struct FieldWord {
  uint32_t id=0,raw_value=0;
  bool operator==(const FieldWord&) const = default;
};
struct Envelope {
  Kind kind=Kind::XboxBigEndian;
  std::vector<std::byte> prefix,tail;
  std::vector<FieldWord> fields;
};
// Decode and rebuild the same source envelope, including opaque data.
// Not a semantic campaign serializer: no assumptions about field types.
Envelope DecodeEnvelope(std::span<const std::byte> data,Kind kind);
std::vector<std::byte> EncodeEnvelope(const Envelope& envelope);
Summary Inspect(std::span<const std::byte> data, Kind kind);
Summary Read(const std::filesystem::path& path, Kind kind,
             size_t maximum_bytes=16u<<20);
}
