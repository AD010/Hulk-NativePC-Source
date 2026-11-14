// Original EoR .IND/.ARC resource backend, independent from the recomp GPU.
#include "eor_archive.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace hulk::eor {
namespace {

uint32_t ReadU32(std::span<const std::byte> bytes, size_t pos, ByteOrder order) {
  if (pos > bytes.size() || bytes.size() - pos < 4)
    throw std::runtime_error("EoR index truncated u32");
  const auto a = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[pos]));
  const auto b = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[pos + 1]));
  const auto c = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[pos + 2]));
  const auto d = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[pos + 3]));
  return order == ByteOrder::Little
             ? a | b << 8 | c << 16 | d << 24
             : a << 24 | b << 16 | c << 8 | d;
}

bool ValidHeaderCount(uint32_t count) {
  return count >= 2 && count <= 128 && (count & 1u) == 0;
}

std::string IndexGroupName(std::span<const std::byte> bytes,
                           size_t first, size_t last) {
  if (last < first || last - first > 256)
    throw std::runtime_error("Malformed EoR index group name range");
  std::string result;
  for (size_t i = first; i < last; ++i) {
    unsigned char b = std::to_integer<uint8_t>(bytes[i]);
    if (!b) break;
    if (b < 0x21 || b > 0x7e || b == '/' || b == '\\')
      throw std::runtime_error("Invalid EoR group name");
    result.push_back(static_cast<char>(b));
    if (result.size() > 64)
      throw std::runtime_error("Overlong EoR group name");
  }
  if (result.empty())
    throw std::runtime_error("Empty EoR group name");
  return result;
}

}  // namespace

uint32_t ResourceHash(std::string_view input) {
  if (input.empty() || input.size() > 128)
    throw std::invalid_argument("EoR resource name length");
  std::string upper;
  upper.reserve(input.size());
  for (unsigned char byte : input) {
    if (byte < 0x21 || byte > 0x7e || byte == '/' || byte == '\\')
      throw std::invalid_argument("EoR resource name must be safe ASCII");
    upper.push_back(
        static_cast<char>((byte >= 'a' && byte <= 'z') ? byte - 32 : byte));
  }
  // EoR _app_ stores TXFLui_*/LFXTui_* typed authoring names. The
  // index hash is of the canonical 'ui_*' stem, not its type marker.
  if (upper.size() > 7 &&
      (upper.compare(0, 4, "TXFL") == 0 ||
       upper.compare(0, 4, "LFXT") == 0) &&
      upper.compare(4, 3, "UI_") == 0) {
    upper.erase(0, 4);
  }
  uint32_t crc = 0xffffffffu;
  for (unsigned char byte : upper) {
    crc ^= byte;
    for (unsigned shift = 0; shift < 8; ++shift)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

Index Index::Decode(std::span<const std::byte> bytes) {
  if (bytes.size() < 24)
    throw std::runtime_error("EoR index header too short");
  const uint32_t little = ReadU32(bytes, 0, ByteOrder::Little);
  const uint32_t big = ReadU32(bytes, 0, ByteOrder::Big);
  const bool valid_le = ValidHeaderCount(little);
  const bool valid_be = ValidHeaderCount(big);
  if (valid_le == valid_be)
    throw std::runtime_error("EoR index byte order unknown or ambiguous");
  Index index;
  index.byte_order_ = valid_le ? ByteOrder::Little : ByteOrder::Big;
  const uint32_t block_count = valid_le ? little : big;
  const size_t offsets_count = static_cast<size_t>(block_count) + 1;
  const size_t header_end = 4 + 4 * offsets_count;
  if (header_end > bytes.size() - 4)
    throw std::runtime_error("EoR index offset table truncated");
  std::vector<uint32_t> offsets(offsets_count);
  for (size_t i = 0; i < offsets_count; ++i)
    offsets[i] = ReadU32(bytes, 4 + 4 * i, index.byte_order_);
  if (offsets.front() < header_end ||
      offsets.back() != bytes.size() - 4)
    throw std::runtime_error("Invalid EoR index footer offset");
  for (size_t i = 1; i < offsets_count; ++i) {
    if (offsets[i] <= offsets[i - 1] || offsets[i] > bytes.size() - 4)
      throw std::runtime_error("Overlapping EoR index blocks");
  }
  index.footer_ = ReadU32(bytes, offsets.back(), index.byte_order_);
  for (size_t group = 0; group < block_count / 2; ++group) {
    const size_t name_start = offsets[2 * group];
    const size_t table_start = offsets[2 * group + 1];
    const size_t table_end = offsets[2 * group + 2];
    auto name = IndexGroupName(bytes, name_start, table_start);
    if (table_end - table_start < 4)
      throw std::runtime_error("EoR resource table has no count");
    const uint32_t count = ReadU32(bytes, table_start, index.byte_order_);
    if (count > 100000 ||
        static_cast<uint64_t>(table_end - table_start) !=
            4ull + 16ull * count)
      throw std::runtime_error("EoR resource table size mismatch");
    std::vector<Resource> records;
    records.reserve(count);
    const size_t metadata_start = table_start + 4 + 4 * count;
    uint32_t previous = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t hash =
          ReadU32(bytes, table_start + 4 + 4 * i, index.byte_order_);
      if (i != 0 && hash <= previous)
        throw std::runtime_error("Unsorted EoR resource hashes");
      previous = hash;
      const size_t location = metadata_start + 12 * i;
      records.push_back(Resource{
          hash, ReadU32(bytes, location, index.byte_order_),
          ReadU32(bytes, location + 4, index.byte_order_),
          ReadU32(bytes, location + 8, index.byte_order_)});
    }
    if (!index.groups_.emplace(std::move(name), std::move(records)).second)
      throw std::runtime_error("Duplicate EoR resource group");
  }
  return index;
}

Index Index::Load(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("Cannot open EoR index file");
  const auto size = file.tellg();
  if (size < 0 || static_cast<uint64_t>(size) > (8u << 20))
    throw std::runtime_error("Unreasonable EoR index size");
  std::vector<std::byte> bytes(static_cast<size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!file) throw std::runtime_error("Cannot read complete EoR index");
  return Decode(bytes);
}

std::optional<Resource> Index::FindHash(std::string_view group,
                                         uint32_t hash) const {
  const auto it = groups_.find(std::string(group));
  if (it == groups_.end()) return std::nullopt;
  const auto& records = it->second;
  const auto item = std::lower_bound(
      records.begin(), records.end(), hash,
      [](const Resource& a, uint32_t b) { return a.hash < b; });
  if (item == records.end() || item->hash != hash) return std::nullopt;
  return *item;
}

std::optional<Resource> Index::FindName(
    std::string_view group, std::string_view resource_name) const {
  return FindHash(group, ResourceHash(resource_name));
}

std::vector<std::byte> ReadResource(const std::filesystem::path& archive,
                                    const Resource& resource,
                                    size_t maximum_size) {
  if (resource.offset < 4 || resource.size == 0 ||
      resource.size > maximum_size)
    throw std::runtime_error("Unsafe EoR archive resource bounds");
  std::error_code ec;
  const auto bytes_on_disk = std::filesystem::file_size(archive, ec);
  if (ec || static_cast<uint64_t>(resource.offset) + resource.size >
                bytes_on_disk)
    throw std::runtime_error("EoR resource extends beyond archive");
  std::ifstream file(archive, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot open EoR archive");
  file.seekg(static_cast<std::streamoff>(resource.offset));
  std::vector<std::byte> data(resource.size);
  file.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
  if (!file) throw std::runtime_error("Failed to read EoR resource data");
  return data;
}

}  // namespace hulk::eor
