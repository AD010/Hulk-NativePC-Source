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
// A live XDK shader object can be linked to a source SHA only when the
// ORIGINAL guest object -> ShaderContainer header pointer ABI is proven,
// and the exact guest header bytes are captured from that pointer.
// The object address alone, a ShaderEffects class name, or a pass index
// cannot establish the microcode digest.
struct OriginalLiveXdkShaderHeaderWitness {
  uint32_t original_xdk_shader_guest_object=0;
  uint32_t original_virtual_header_guest_address=0;
  bool original_object_to_header_abi_verified=false;
  std::vector<uint8_t> exact_original_guest_virtual_header;
  // The exact XDK Draw epoch/ordinal that sampled the original header.
  // A shader object address may be reused after memory/device changes.
  uint64_t original_frame_sequence=0;
  uint32_t original_call_ordinal=UINT32_MAX;
};
// Probe the second word of an independently verified original 8-byte
// EoR VS/PS handle-table row. The original PPC only proves word 0 is the
// shader object; word 1 is an UNTRUSTED candidate until the byte-exact
// ShaderContainer header resolves to a unique HEORVRT1 source digest.
// The probe is read-only and may reject every call on unsupported builds.
// Only enabled for exact live stage object + current Present/Draw.
using ReadOriginalGuestShaderBytes=bool (*)(
    void*,uint32_t,std::byte*,size_t);
std::optional<OriginalLiveXdkShaderHeaderWitness>
ObserveOriginalLiveXdkShaderHandleEntry(
    const OriginalVirtualShaderIdentity& identities,
    uint32_t original_eor_handle_row_guest,
    uint32_t current_bound_xdk_shader_object,
    Stage stage,
    ReadOriginalGuestShaderBytes reader,void* original_guest_base,
    uint64_t exact_frame_sequence,uint32_t exact_call_ordinal) noexcept;
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
