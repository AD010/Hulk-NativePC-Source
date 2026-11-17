// Actual USA XDK call provenance, NOT a fabricated EoR Draw submission.
// Keep original PPC call-through and memory untouched. Opt-in audit only.
#pragma once
#include "eor_xdk_guest_constants.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <optional>
#include <span>
#include <tuple>

namespace hulk::eor::xdk {
enum class Kind : uint8_t { DrawVertices, DrawIndexedVertices, DrawVerticesUP };
struct RawDraw {
  Kind kind{};
  uint32_t function = 0, lr = 0;
  std::array<uint32_t, 8> gpr{}; // r3..r10 from original USA PPC call.
  // Read BEFORE the guest Draw mutates the XDK device shadow state.
  // A missing snapshot is not a verified camera or constant ABI.
  std::optional<OriginalVsMatrices> observed_vs_matrices;
};
// Verified USA XDK 2.0.6534 method ABI from the original wrapper:
// DrawIndexedVertices(dev, prim, baseVertex, startIndex, indexCount).
// The primitive enum and guest buffer ownership remain real game state,
// NOT host Vulkan layout or model identity.
struct IndexedXdkCall {
  uint32_t xdk_function=0, caller_lr=0, guest_device=0;
  uint32_t primitive=0, first_index=0, index_count=0;
  int32_t base_vertex=0;
};
std::optional<IndexedXdkCall> DecodeOriginalIndexedXdkCall(const RawDraw& call);
// Verified XDK 6534 DrawVertices(dev, prim, startVertex, vertexCount).
// No UP/inline draw is inferred, and triangle-list topology must be
// independently matched to real source model/index data.
struct VerticesXdkCall {
  uint32_t xdk_function=0, caller_lr=0, guest_device=0;
  uint32_t primitive=0, start_vertex=0, vertex_count=0;
};
std::optional<VerticesXdkCall> DecodeOriginalVerticesXdkCall(const RawDraw& call);
struct Frame {
  uint64_t sequence = 0;
  uint32_t dropped = 0;
  std::vector<RawDraw> calls;
};
// Single-slot frame handoff for the native renderer. Never block guest
// Present waiting for Vulkan; any overwritten frame is explicitly counted.
struct DeliveredFrame {
  Frame frame;
  uint64_t overwritten_since_take=0;
};
class CompletedFrameMailbox {
 public:
  void Publish(Frame frame);
  std::optional<DeliveredFrame> Take();
  uint64_t TotalOverwritten() const noexcept {return total_overwritten_;}
  void Reset() noexcept;
 private:
  std::optional<Frame> pending_;
  uint64_t total_overwritten_=0;
  uint64_t missed_since_take_=0;
};
class FrameCapture {
 public:
  static constexpr std::size_t kMaxDraws = 8192;
  void Record(const RawDraw& call);
  Frame Finish();
  void Reset();
 private:
  uint64_t seq_ = 0;
  uint32_t dropped_ = 0;
  std::vector<RawDraw> calls_;
};
// Read-only source-proven guest callsite frequency and ABI variability.
// An LR identifies a callsite lead, NOT an original engine method or layout.
struct CallsiteEvidenceRecord {
  Kind kind{};
  uint32_t xdk_function=0;
  uint32_t caller_lr=0;
  uint64_t recorded_draws=0;
  uint64_t frames_seen=0;
  uint64_t first_frame=0;
  uint64_t last_frame=0;
  uint32_t varying_gpr_mask=0;
  uint32_t nonzero_gpr_mask=0;
  std::array<uint32_t,8> first_gpr{};
};
class XdkCallsiteEvidence {
 public:
  static constexpr std::size_t kMaxCallsites=256;
  void Observe(const Frame& frame);
  std::vector<CallsiteEvidenceRecord> HotCallsites(std::size_t count=8) const;
  uint64_t DroppedDraws() const noexcept {return dropped_draws_;}
  uint64_t UntrackedCalls() const noexcept {return untracked_calls_;}
  void Reset() noexcept;
 private:
  using Key=std::tuple<Kind,uint32_t,uint32_t>;
  std::map<Key,CallsiteEvidenceRecord> records_;
  uint64_t last_sequence_=0;
  uint64_t dropped_draws_=0;
  uint64_t untracked_calls_=0;
};
// These methods are only called from verified hulk_native_x_profile.ini
// addresses in the AOT USA wrappers. Raw PPC r3..r10 are not host VB/IB
// identities or rasterizable shader programs.
}  // namespace hulk::eor::xdk
