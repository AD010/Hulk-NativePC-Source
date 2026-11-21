#pragma once
// Exact ORIGINAL Xbox ShaderEffects virtual-header SHA-256 -> ORIGINAL
// full Xenos shader SHA-256. The user owns a private HEORVRT1 bank built
// from all original ShaderEffects containers. No original program bytes
// are embedded or shipped in source. Does NOT infer which currently bound
// XDK shader object owns a recovered guest header.
#include "eor_spirv_bank.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>

namespace hulk::eor::aot {
Digest OriginalShaderVirtualSha256(std::span<const uint8_t> bytes) noexcept;
class OriginalVirtualShaderIdentity {
 public:
  // HEORVRT1 + [count:LE32, version:LE32] +
  // each [virtual_sha256, original_full_source_sha256,
  //       original_stage:LE32, exact_virtual_header_bytes:LE32].
  // Original virtual digests must be unique across source shaders.
  void Load(const std::filesystem::path& private_bank);
  // Hash *all* original metadata bytes of the guest-visible Xenos
  // ShaderContainer virtual header. Reject truncated/oversized/wrong
  // stage/unknown/ambiguous source. A copied archive buffer is NOT proof
  // that a live original XDK shader device object points at these bytes.
  std::optional<Digest> FindOriginalSource(
      std::span<const uint8_t> exact_original_virtual_header,
      Stage expected_stage) const noexcept;
  size_t size() const noexcept {return original_.size();}
  void Clear() noexcept {original_.clear();}
 private:
  struct Entry {Digest full_source_sha{};Stage stage=Stage::Vertex;
                uint32_t virtual_bytes=0;};
  std::map<Digest,Entry> original_;
};
} // namespace hulk::eor::aot
