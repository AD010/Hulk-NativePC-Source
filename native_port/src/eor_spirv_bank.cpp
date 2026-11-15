#include "eor_spirv_bank.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace hulk::eor::aot {
namespace {
constexpr size_t kMaxPackedBytes = 1024ull * 1024u * 1024u;
constexpr size_t kMaxShaders = 16384;
constexpr uint32_t kSpirvMagic = 0x07230203;
uint32_t Read32(std::span<const uint8_t> bytes, size_t& offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    throw std::invalid_argument("Truncated private shader-bank index");
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
    value |= uint32_t(bytes[offset + i]) << (i * 8);
  offset += 4;
  return value;
}
} // namespace

void ShaderBank::Load(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) throw std::invalid_argument("Private AOT shader bank missing");
  const auto total = f.tellg();
  if (total < 0 || uint64_t(total) < 16u || uint64_t(total) > kMaxPackedBytes)
    throw std::invalid_argument("Invalid private AOT bank size");
  std::vector<uint8_t> bytes(static_cast<size_t>(total));
  f.seekg(0);
  if (!f.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size())))
    throw std::invalid_argument("Truncated private AOT bank file");
  const std::span<const uint8_t> data(bytes.data(), bytes.size());
  if (std::memcmp(data.data(), "HEORSPV1", 8) != 0)
    throw std::invalid_argument("Unknown EoR SPIR-V cache version");
  size_t offset = 8;
  const uint32_t expected = Read32(data, offset);
  const uint32_t compiled = Read32(data, offset);
  if (expected == 0 || expected > kMaxShaders || compiled > expected)
    throw std::invalid_argument("Invalid EoR shader-cache coverage");
  std::map<Digest, Blob> next;
  for (uint32_t i = 0; i < compiled; ++i) {
    if (offset > data.size() || data.size() - offset < 40)
      throw std::invalid_argument("Truncated EoR shader-cache record");
    Digest key{};
    for (size_t b = 0; b < key.size(); ++b) key[b] = data[offset + b];
    offset += key.size();
    const uint32_t stage = Read32(data, offset);
    const uint32_t words = Read32(data, offset);
    if ((stage != 1 && stage != 2) || words < 5 || words > (8u << 20) / 4 ||
        offset > data.size() || size_t(words) > (data.size() - offset) / 4)
      throw std::invalid_argument("Invalid EoR SPIR-V shader record");
    Blob blob;
    blob.stage = static_cast<Stage>(stage);
    blob.spirv.reserve(words);
    for (uint32_t j = 0; j < words; ++j) blob.spirv.push_back(Read32(data, offset));
    if (blob.spirv[0] != kSpirvMagic ||
        blob.spirv[1] < 0x10000u || blob.spirv[1] > 0x10600u ||
        blob.spirv[3] == 0 || blob.spirv[3] > 1000000 ||
        blob.spirv[4] != 0)
      throw std::invalid_argument("Invalid SPIR-V module header in private bank");
    if (!next.emplace(key, std::move(blob)).second)
      throw std::invalid_argument("Duplicate Xenos source SHA in private AOT bank");
  }
  if (offset != data.size())
    throw std::invalid_argument("Trailing unvalidated shader-cache data");
  expected_ = expected;
  shaders_.swap(next); // Existing good shader bank survives a bad reload.
}

const Blob* ShaderBank::Find(const Digest& sha, Stage stage) const {
  auto it = shaders_.find(sha);
  return it != shaders_.end() && it->second.stage == stage ? &it->second : nullptr;
}
} // namespace hulk::eor::aot
