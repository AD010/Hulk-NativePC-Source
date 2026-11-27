#include "eor_spirv_bank.h"
#include "eor_spirv_static_registers.h"
#include "eor_spirv_clip_position.h"
#include <cstring>
#include <fstream>
#include <bit>
#include <stdexcept>

namespace hulk::eor::aot {
namespace {
constexpr size_t kMaxPackedBytes = 1024ull * 1024u * 1024u;
constexpr size_t kMaxShaders = 16384;
constexpr uint32_t kSpirvMagic = 0x07230203;
uint32_t Read32(const uint8_t* bytes) noexcept {
  return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
         (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
void ReadExact(std::istream& file, void* dst, size_t bytes, size_t& remaining) {
  if (bytes > remaining ||
      !file.read(static_cast<char*>(dst), static_cast<std::streamsize>(bytes)))
    throw std::invalid_argument("Truncated private AOT bank file");
  remaining -= bytes;
}

// Verify instruction WORD COUNTS and the declared pipeline stage before
// opening a Vulkan device. This is a structural safety gate, NOT spirv-val:
// type graphs, resource layouts, capabilities and shader semantics still need
// the offline compiler/validator and an actual EoR material/PSO match.
void CheckStructure(std::span<const uint32_t> words, Stage stage) {
  bool memory_model = false;
  unsigned entry_count = 0;
  const uint32_t required_model = stage == Stage::Vertex ? 0u : 4u;
  for (size_t i = 5; i < words.size();) {
    const uint32_t header = words[i];
    const size_t count = header >> 16;
    const uint32_t opcode = header & 0xffffu;
    if (!count || count > words.size() - i)
      throw std::invalid_argument("Truncated SPIR-V instruction in private bank");
    if (opcode == 14) { // OpMemoryModel
      if (count != 3 || memory_model)
        throw std::invalid_argument("Invalid SPIR-V memory model instruction");
      memory_model = true;
    } else if (opcode == 15) { // OpEntryPoint
      if (count < 4)
        throw std::invalid_argument("Invalid SPIR-V entry point instruction");
      if (words[i + 1] == required_model) {
        // DiligentAotShaderModules uses EntryPoint="main" unconditionally.
        // The *exact* NUL-terminated string must be "main" (4 bytes + NUL),
        // not a string terminated inside unrelated OpEntryPoint interface
        // IDs. Ambiguous duplicate main-stage entrypoints are unsafe.
        if (count < 5 || words[i + 2] == 0 ||
            words[i + 2] >= words[3] ||
            words[i + 3] != 0x6e69616du || words[i + 4] != 0 ||
            ++entry_count != 1)
          throw std::invalid_argument(
              "Original EoR SPIR-V requires one exact main entry point");
        for (size_t j = i + 5; j < i + count; ++j)
          if (words[j] == 0 || words[j] >= words[3])
            throw std::invalid_argument("Invalid original SPIR-V interface ID");
      }
    }
    i += count;
  }
  if (!memory_model || entry_count != 1)
    throw std::invalid_argument("Missing matching vertex/fragment SPIR-V entry point");
}
} // namespace

void ShaderBank::Load(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) throw std::invalid_argument("Private AOT shader bank missing");
  const auto total = f.tellg();
  if (total < 0 || uint64_t(total) < 16u || uint64_t(total) > kMaxPackedBytes)
    throw std::invalid_argument("Invalid private AOT bank size");
  size_t remaining = static_cast<size_t>(total);
  f.seekg(0);
  std::array<uint8_t,16> header{};
  ReadExact(f, header.data(), header.size(), remaining);
  if (std::memcmp(header.data(), "HEORSPV1", 8) != 0)
    throw std::invalid_argument("Unknown EoR SPIR-V cache version");
  const uint32_t expected = Read32(header.data()+8);
  const uint32_t compiled = Read32(header.data()+12);
  if (expected == 0 || expected > kMaxShaders || compiled > expected)
    throw std::invalid_argument("Invalid EoR shader-cache coverage");
  std::map<Digest, Blob> next;
  for (uint32_t i = 0; i < compiled; ++i) {
    std::array<uint8_t,40> record{};
    ReadExact(f, record.data(), record.size(), remaining);
    Digest key{};
    std::memcpy(key.data(), record.data(), key.size());
    const uint32_t stage = Read32(record.data()+32);
    const uint32_t words = Read32(record.data()+36);
    if ((stage != 1 && stage != 2) || words < 5 || words > (8u << 20) / 4 ||
        size_t(words) > remaining / 4)
      throw std::invalid_argument("Invalid EoR SPIR-V shader record");
    Blob blob;
    blob.stage = static_cast<Stage>(stage);
    // Read directly into the final module allocation. A partial-bank
    // refresh already retains the previous generation until validation;
    // do not also retain a full temporary copy of the packed file.
    blob.spirv.resize(words);
    ReadExact(f, blob.spirv.data(), size_t(words)*4, remaining);
    if constexpr (std::endian::native != std::endian::little) {
      for (auto& word : blob.spirv)
        word = Read32(reinterpret_cast<const uint8_t*>(&word));
    }
    if (blob.spirv[0] != kSpirvMagic ||
        blob.spirv[1] < 0x10000u || blob.spirv[1] > 0x10600u ||
        blob.spirv[3] == 0 || blob.spirv[3] > 1000000 ||
        blob.spirv[4] != 0)
      throw std::invalid_argument("Invalid SPIR-V module header in private bank");
    CheckStructure(blob.spirv, blob.stage);
    if(blob.stage==Stage::Vertex) {
      blob.scalar_register_reads=AnalyzeScalarRegisterReads(blob.spirv);
      blob.scalar_preserves_clip_xyw=ScalarPreservesClipXYW(blob.spirv);
    }
    if (!next.emplace(key, std::move(blob)).second)
      throw std::invalid_argument("Duplicate Xenos source SHA in private AOT bank");
  }
  if (remaining != 0 || f.peek() != std::char_traits<char>::eof() || f.bad())
    throw std::invalid_argument("Trailing unvalidated shader-cache data");
  expected_ = expected;
  shaders_.swap(next); // Existing good shader bank survives a bad reload.
}

const Blob* ShaderBank::Find(const Digest& sha, Stage stage) const {
  auto it = shaders_.find(sha);
  return it != shaders_.end() && it->second.stage == stage ? &it->second : nullptr;
}
} // namespace hulk::eor::aot
