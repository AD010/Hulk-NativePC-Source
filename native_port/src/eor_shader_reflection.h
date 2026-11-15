// Original Xbox ShaderEffects constant register reflection, no Xenos runtime.
// HEORREF1 stores SHA-256 identifiers and original constant-register slots.
// Reflecting ViewProj is NOT equivalent to hooking the engine's camera ABI.
#pragma once
#include "eor_spirv_bank.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace hulk::eor::aot {
struct RegisterSpan {
  uint32_t index = UINT32_MAX, count = 0;
  bool Present() const noexcept { return index != UINT32_MAX && count != 0; }
};
struct ReflectedShader {
  Stage stage = Stage::Vertex;
  RegisterSpan view_projection, world, previous_world, sky_view_projection;
};
class ReflectionBank {
 public:
  void Load(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::invalid_argument("Private EoR shader reflection unavailable");
    const auto length = f.tellg();
    if (length < 16 || length > (16ll + 68ll * 16384))
      throw std::invalid_argument("Invalid original EoR shader reflection size");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))
      throw std::invalid_argument("Truncated original shader reflection");
    if (std::string(bytes.begin(), bytes.begin() + 8) != "HEORREF1")
      throw std::invalid_argument("Unexpected original shader reflection version");
    const auto u32 = [&](size_t at) -> uint32_t {
      return uint32_t(bytes[at]) | (uint32_t(bytes[at+1])<<8) |
             (uint32_t(bytes[at+2])<<16) | (uint32_t(bytes[at+3])<<24);
    };
    const auto n = u32(8), version = u32(12);
    if (n == 0 || n > 16384 || version != 1 || bytes.size() != 16ull + 68ull * n)
      throw std::invalid_argument("Invalid original shader reflection count");
    std::map<Digest, ReflectedShader> next;
    for (uint32_t i = 0; i < n; ++i) {
      const size_t at = 16ull + size_t(i) * 68;
      Digest id{};
      for (size_t k = 0; k < id.size(); ++k) id[k] = bytes[at+k];
      const uint32_t stage = u32(at+32);
      if (stage != 1 && stage != 2)
        throw std::invalid_argument("Invalid original shader stage");
      ReflectedShader r;
      r.stage = stage == 1 ? Stage::Vertex : Stage::Pixel;
      RegisterSpan* spans[] = {&r.view_projection, &r.world,
                               &r.previous_world, &r.sky_view_projection};
      for (size_t k = 0; k < 4; ++k) {
        auto* span = spans[k];
        span->index = u32(at+36+8*k);
        span->count = u32(at+40+8*k);
        const bool valid_span = span->index == UINT32_MAX
            ? span->count == 0
            : (r.stage == Stage::Vertex &&
               (span->count == 4 || (k == 1 && span->count == 3)) &&
               span->index <= 256u - span->count);
        if (!valid_span)
          throw std::invalid_argument("Invalid native 4-register VS matrix");
      }
      if (!next.emplace(id,r).second)
        throw std::invalid_argument("Duplicate reflected original shader");
    }
    entries_.swap(next); // Bad reflection cannot replace previous good data.
  }
  const ReflectedShader* Find(const Digest& id, Stage stage) const noexcept {
    auto it = entries_.find(id);
    return it != entries_.end() && it->second.stage == stage ? &it->second : nullptr;
  }
  size_t size() const noexcept { return entries_.size(); }
 private:
  std::map<Digest, ReflectedShader> entries_;
};
} // namespace hulk::eor::aot
