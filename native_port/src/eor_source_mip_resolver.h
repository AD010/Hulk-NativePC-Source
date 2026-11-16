#pragma once
// Authenticated mapping between the actual NativeX Xbox GPU texture read
// and source owner metadata from the original USA LFXT/ARC. No original
// texture pixels or user data are bundled. Never pick an arbitrary source
// if multiple assets have identical raw mip content.
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace hulk::eor::gpu {
struct OriginalMipWitness {
  uint32_t xenos_format=0, width=0, height=0;
  uint64_t full_xxh3=0, first_fnv=0, second_fnv=0;
  friend bool operator==(const OriginalMipWitness&,
                         const OriginalMipWitness&)=default;
};
struct OriginalMipIdentity {
  OriginalMipWitness witness{};
  uint32_t original_texture_hash=0;
  uint32_t original_mip_level=0;
  bool archived_stream=false;
};
class VerifiedSourceMipResolver {
 public:
  explicit VerifiedSourceMipResolver(std::span<const OriginalMipIdentity> original);
  // Source hash is returned ONLY when every matching original resource
  // represents the same actual image; no fallback for unknown source.
  std::optional<OriginalMipIdentity> Find(
      const OriginalMipWitness& real_gpu_read) const;
  size_t ambiguous_witnesses() const noexcept { return ambiguous_; }
 private:
  std::vector<OriginalMipIdentity> original_;
  size_t ambiguous_=0;
};
} // namespace hulk::eor::gpu
