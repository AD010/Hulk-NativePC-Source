// Private, verified AOT SPIR-V bank from original EoR shader containers.
// Vulkan-ready modules are indexed by the SHA-256 of the ORIGINAL Xenos
// ShaderContainer, not by mutable guest pointers or legacy PM4 register IDs.
// Does not create GPU PSOs until vertex layouts and descriptors are verified.
#pragma once
#include <array>
#include <bitset>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <vector>

namespace hulk::eor::aot {
enum class Stage : uint32_t { Vertex = 1, Pixel = 2 };
using Digest = std::array<uint8_t, 32>;
struct Blob {
  Stage stage = Stage::Vertex;
  std::vector<uint32_t> spirv;
  // Derived once from immutable, structurally verified AOT bytecode.
  // Synthetic caller-created Blobs can omit these and use uncached analysis.
  std::optional<std::bitset<256>> scalar_register_reads{};
  std::optional<bool> scalar_preserves_clip_xyw{};
};
class ShaderBank {
 public:
  // HEORSPV1 + expected/compiled counts + [source SHA, stage, words, SPIR-V].
  // Throws on corrupt payloads, duplicate keys, unsupported SPIR-V and tails.
  void Load(const std::filesystem::path& private_bank);
  const Blob* Find(const Digest& shader_sha256, Stage stage) const;
  size_t size() const noexcept { return shaders_.size(); }
  size_t expected() const noexcept { return expected_; }
  bool HasFullArchiveCoverage() const noexcept {
    return expected_ > 0 && expected_ == shaders_.size();
  }
  void Clear() noexcept { shaders_.clear(); expected_ = 0; }
 private:
  size_t expected_ = 0;
  std::map<Digest, Blob> shaders_;
};
} // namespace hulk::eor::aot
