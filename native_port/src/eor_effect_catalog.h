// Native EoR original ShaderEffects -> Xenos shader identity ledger.
// Metadata contains only original effect hashes and source SHA-256;
// no original game shader bytes, ReXGlue, PM4 or Vulkan dependency.
#pragma once
#include "eor_spirv_bank.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hulk::eor::aot {
class EffectCatalog {
 public:
  void Load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::invalid_argument("Private original EoR effect ledger absent");
    const auto length = file.tellg();
    if (length < 16 || length > (16 + 40 * 16384))
      throw std::invalid_argument("Invalid EoR effect ledger file size");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())))
      throw std::invalid_argument("Truncated EoR effect ledger");
    if (std::string(bytes.begin(), bytes.begin() + 8) != "HEORMAT1")
      throw std::invalid_argument("Unsupported EoR effect ledger version");
    const auto read32 = [&](size_t at) -> uint32_t {
      return uint32_t(bytes[at]) | (uint32_t(bytes[at+1]) << 8) |
             (uint32_t(bytes[at+2]) << 16) | (uint32_t(bytes[at+3]) << 24);
    };
    const uint32_t count = read32(8), version = read32(12);
    if (version != 1 || count == 0 || count > 16384 ||
        bytes.size() != 16ull + 40ull * count)
      throw std::invalid_argument("Invalid original EoR ledger record count");
    std::map<uint32_t, std::array<std::set<Digest>,2>> candidate;
    std::set<Digest> unique;
    for (uint32_t i = 0; i < count; ++i) {
      const size_t at = 16ull + 40ull * i;
      const uint32_t effect = read32(at), stage = read32(at + 4);
      if (!effect || (stage != 1 && stage != 2))
        throw std::invalid_argument("Invalid original EoR effect/stage identity");
      Digest digest{};
      for (size_t b = 0; b < digest.size(); ++b) digest[b] = bytes[at + 8 + b];
      if (!unique.insert(digest).second ||
          !candidate[effect][stage - 1].insert(digest).second)
        throw std::invalid_argument("Duplicate original Xenos shader identity");
    }
    for (const auto& [effect, stages] : candidate)
      if (stages[0].empty() || stages[1].empty())
        throw std::invalid_argument("Effect lacks original VS/PS pairing");
    sources_.swap(candidate); // retain previous good catalog if Load fails
    shader_count_ = count;
  }
  bool HasPair(uint32_t effect_hash, const Digest& vs, const Digest& ps) const {
    auto it = sources_.find(effect_hash);
    return it != sources_.end() && it->second[0].contains(vs) &&
           it->second[1].contains(ps);
  }
  size_t EffectCount() const noexcept { return sources_.size(); }
  size_t ShaderCount() const noexcept { return shader_count_; }
 private:
  size_t shader_count_ = 0;
  std::map<uint32_t, std::array<std::set<Digest>,2>> sources_;
};
} // namespace hulk::eor::aot
