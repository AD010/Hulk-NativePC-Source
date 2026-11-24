#pragma once
// Exact ORIGINAL Xbox ShaderEffects virtual-header SHA-256 -> ORIGINAL
// full Xenos shader SHA-256. The user owns a private HEORVRT1 bank built
// from all original ShaderEffects containers. No original program bytes
// are embedded or shipped in source. Does NOT infer which currently bound
// XDK shader object owns a recovered guest header.
#include "eor_spirv_bank.h"
#include "eor_effect_catalog.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace hulk::eor::aot {
Digest SourceSha256(std::span<const uint8_t> bytes) noexcept;
inline Digest OriginalShaderVirtualSha256(std::span<const uint8_t> bytes) noexcept {
  return SourceSha256(bytes);
}
// The original ShaderContainer's UPDB filename is debug-only metadata.
// It is NOT shader code, constants, resource declarations or instruction
// descriptors. All other header bytes and the complete physical program
// must still match the authenticated source before admitting an alias.
std::optional<Digest> OriginalPixelDebugHeaderKey(
    std::span<const uint8_t> header) noexcept;
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
  struct PixelDebugAlias {
    Digest full_source_sha{},physical_sha{};
    std::vector<uint8_t> original_header;
  };
  // Optional, read-only source archive from the user's existing USA game.
  // No new asset download, compilation, or public shader binary is needed.
  size_t LoadOriginalPixelDebugAliases(const std::filesystem::path& game_root) noexcept;
  bool HasPixelDebugCandidate(std::span<const uint8_t> header) const noexcept;
  const PixelDebugAlias* FindPixelDebugAlias(
      std::span<const uint8_t> header,std::span<const uint8_t> physical) const noexcept;
  size_t PixelDebugAliasCount() const noexcept {return pixel_alias_count_;}
  size_t size() const noexcept {return original_.size();}
  // Two independently loaded, fully authenticated HEORVRT1 catalogues
  // must compare by their original immutable SHA mappings, NOT the C++
  // shared_ptr address. Startup AOT loading may create a second copy of
  // the same pinned original USA metadata bank on a different thread.
  bool SameOriginalSources(const OriginalVirtualShaderIdentity& rhs) const noexcept {
    return original_==rhs.original_;
  }
  void Clear() noexcept {original_.clear();pixel_debug_aliases_.clear();pixel_alias_count_=0;}
 private:
  struct Entry {Digest full_source_sha{};Stage stage=Stage::Vertex;
                uint32_t virtual_bytes=0;
                bool operator==(const Entry&) const = default;};
  std::map<Digest,Entry> original_;
  std::map<Digest,std::vector<PixelDebugAlias>> pixel_debug_aliases_;
  size_t pixel_alias_count_=0;
};
// A live XDK shader object can be linked to a source SHA only when the
// ORIGINAL guest object -> ShaderContainer header pointer ABI is proven,
// and the exact guest header bytes are captured from that pointer.
// The object address alone, a ShaderEffects class name, or a pass index
// cannot establish the microcode digest.
struct OriginalPixelSamplerControls {
  Digest source{};
  uint16_t sampled_slots=0, point_slots=0;
};
// Decode the filter controls in executable CF EXEC fetches only. A whole
// source SHA match is required before any override can affect native draws.
std::optional<OriginalPixelSamplerControls> ReadOriginalPixelSamplerControls(
    std::span<const uint8_t> container,const Digest& expected_source);
struct VerifiedPixelSamplerProgram {
  OriginalPixelSamplerControls controls;
  std::vector<uint8_t> container;
};
using OriginalPixelSamplerCache=std::map<Digest,VerifiedPixelSamplerProgram>;
struct OriginalLiveXdkShaderHeaderWitness {
  uint32_t original_xdk_shader_guest_object=0;
  uint32_t original_virtual_header_guest_address=0;
  bool original_object_to_header_abi_verified=false;
  std::vector<uint8_t> exact_original_guest_virtual_header;
  // The exact XDK Draw epoch/ordinal that sampled the original header.
  // A shader object address may be reused after memory/device changes.
  uint64_t original_frame_sequence=0;
  uint32_t original_call_ordinal=UINT32_MAX;
  std::optional<OriginalPixelSamplerControls> pixel_sampling{};
  // Present only for an independently source-authenticated debug-path
  // alias. These are actual live physical bytes, never copied from ARC.
  std::vector<uint8_t> alias_live_physical;
};
// Read the inline ShaderContainer of the current USA XDK shader object and
// establish archive ownership from the SHA-256 of the complete virtual bytes.
using ReadOriginalGuestShaderBytes=bool (*)(
    void*,uint32_t,std::byte*,size_t);
std::optional<OriginalLiveXdkShaderHeaderWitness>
ObserveOriginalBoundXdkShader(
    const OriginalVirtualShaderIdentity& identities,
    uint32_t current_bound_xdk_shader_object,
    Stage stage,
    ReadOriginalGuestShaderBytes reader,void* original_guest_base,
    uint64_t exact_frame_sequence,uint32_t exact_call_ordinal) noexcept;
std::optional<OriginalPixelSamplerControls> CaptureOriginalPixelSamplerControls(
    const OriginalLiveXdkShaderHeaderWitness& witness,const Digest& original_source,
    ReadOriginalGuestShaderBytes read,void* state,OriginalPixelSamplerCache& cache,
    const OriginalVirtualShaderIdentity* identities=nullptr);
// Decode the actual source digest from an original, byte-exact live
// ShaderContainer header, only if object/header pointer ABI and the
// exact original XDK Present ordinal are independently authenticated.
std::optional<Digest> ResolveOriginalLiveXdkShaderSource(
    const OriginalVirtualShaderIdentity& identities,
    const OriginalLiveXdkShaderHeaderWitness& source,
    uint32_t currently_bound_xdk_shader_object,
    Stage expected_stage,
    uint64_t exact_frame_sequence, uint32_t exact_call_ordinal) noexcept;
// Resolve the genuine ShaderEffects effect/pass from both XDK shader objects.
// The original effect ledger rejects duplicate source VS/PS pairs; there is
// no name-based class->effect shortcut or guessed technique index.
std::optional<OriginalEffectPass> ResolveOriginalLiveEorEffectPass(
    const OriginalVirtualShaderIdentity& identities,
    const EffectCatalog& effects,
    const OriginalLiveXdkShaderHeaderWitness& vertex,
    const OriginalLiveXdkShaderHeaderWitness& pixel,
    uint32_t bound_vertex_xdk_object,uint32_t bound_pixel_xdk_object,
    uint64_t exact_frame_sequence,uint32_t exact_call_ordinal) noexcept;
// Check stage, exact live object and byte-identical HEORVRT1 digest before
// accepting a runtime shader binding. No guessed object offsets are read.
bool MatchesOriginalLiveXdkShaderSource(
    const OriginalVirtualShaderIdentity& identities,
    const OriginalLiveXdkShaderHeaderWitness& source,
    uint32_t currently_bound_xdk_shader_object,
    Stage expected_stage,
    const Digest& expected_original_full_shader_sha256,
    uint64_t exact_frame_sequence, uint32_t exact_call_ordinal) noexcept;
} // namespace hulk::eor::aot
