// EoR original ShaderEffects: strict offline VS/PS ordinal-pair catalog.
// SHADER BYTECODE IS NEVER STORED HERE. Ordinal pairing is a candidate
// extracted from the original effect's ordered stage groups; per-draw ABI
// and render-pass identity still must be verified before any Vulkan Draw.
#pragma once
#include "eor_spirv_bank.h"
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
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::invalid_argument("Private original EoR effect ledger absent");
    const auto total = f.tellg();
    if (total < 16 || total > (16ll + 72ll * 16384))
      throw std::invalid_argument("Invalid EoR effect ledger size");
    std::vector<uint8_t> bytes(static_cast<size_t>(total));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))
      throw std::invalid_argument("Truncated EoR effect ledger");
    if (std::string(bytes.begin(), bytes.begin() + 8) != "HEORMAT2")
      throw std::invalid_argument("Legacy unpaired HEORMAT1 is unsafe: regenerate HEORMAT2");
    const auto read32 = [&](size_t at) -> uint32_t {
      return uint32_t(bytes[at]) | (uint32_t(bytes[at + 1]) << 8) |
             (uint32_t(bytes[at + 2]) << 16) | (uint32_t(bytes[at + 3]) << 24);
    };
    const uint32_t count = read32(8), version = read32(12);
    if (version != 2 || count == 0 || count > 16384 ||
        bytes.size() != 16ull + 72ull * count)
      throw std::invalid_argument("Invalid original EoR paired shader record count");
    using Pair = std::pair<Digest, Digest>;
    std::map<uint32_t, std::vector<Pair>> next;
    // An original shader may be reused by many effects / passes.
    // Only conflicting VS/PS stage assignments are ambiguous.
    std::map<Digest,Stage> unique;
    for (uint32_t i = 0; i < count; ++i) {
      const size_t at = 16ull + size_t(i) * 72;
      const uint32_t effect = read32(at), pass = read32(at + 4);
      if (!effect || pass > 16384)
        throw std::invalid_argument("Original EoR material identity invalid");
      Digest vs{}, ps{};
      for (size_t b = 0; b < 32; ++b) {
        vs[b] = bytes[at + 8 + b];
        ps[b] = bytes[at + 40 + b];
      }
      if (vs == ps)
        throw std::invalid_argument("One original shader identity has incompatible stages");
      const auto accept = [&](const Digest& digest, Stage stage) {
        const auto [it, inserted] = unique.emplace(digest, stage);
        if (!inserted && it->second != stage)
          throw std::invalid_argument("Original Xenos shader identity changes stage");
      };
      accept(vs, Stage::Vertex);
      accept(ps, Stage::Pixel);
      auto& slots = next[effect];
      if (pass != slots.size())
        throw std::invalid_argument("EoR pass slots are missing, reordered or duplicated");
      slots.emplace_back(vs, ps);
    }
    sources_.swap(next); // Failure leaves previous valid catalog untouched.
    pair_count_ = count;
    unique_shader_count_ = unique.size();
  }
  bool HasPair(uint32_t effect_hash, uint32_t pass_index,
               const Digest& vs, const Digest& ps) const noexcept {
    const auto it = sources_.find(effect_hash);
    return it != sources_.end() && pass_index < it->second.size() &&
           it->second[pass_index].first == vs &&
           it->second[pass_index].second == ps;
  }
  size_t EffectCount() const noexcept { return sources_.size(); }
  size_t PairCount() const noexcept { return pair_count_; }
  // Actual number of AOT modules, not the number of draw/material references.
  size_t ShaderCount() const noexcept { return unique_shader_count_; }
 private:
  size_t pair_count_ = 0;
  size_t unique_shader_count_ = 0;
  std::map<uint32_t, std::vector<std::pair<Digest, Digest>>> sources_;
};
} // namespace hulk::eor::aot
