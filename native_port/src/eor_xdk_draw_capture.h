// Actual USA XDK call provenance, NOT a fabricated EoR Draw submission.
// Keep original PPC call-through and memory untouched. Opt-in audit only.
#pragma once
#include "eor_xdk_guest_constants.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <tuple>

namespace hulk::eor::xdk {
enum class Kind : uint8_t { DrawVertices, DrawIndexedVertices, DrawVerticesUP };
// Original actual XDK command chronology is essential for a sole Vulkan
// presenter: Draws alone omit clears, RT/depth changes, tiled resolves and
// postprocessing. The events record real PPC call arguments but intentionally
// do NOT invent the ABI of pointed-to guest rectangle/resolve structures.
enum class CommandKind : uint8_t {
  DrawVertices, DrawIndexedVertices, DrawVerticesUP,
  SetRenderTarget, SetDepthStencilSurface,
  ClearF, Resolve, BeginTiling, EndTiling, Present
};
// Source-verified ClearF(dev, flags, rect, color*, Z in f1, stencil).
// Values are BE-decoded at the REAL PPC call, before guest memory can
// change. Original bound surfaces still require independent ownership proof.
struct OriginalXdkClearF {
  uint32_t flags=0;
  bool has_rect=false,has_color=false;
  std::array<int32_t,4> rect{}; // original x0,y0,x1,y1
  std::array<float,4> color{};
  float depth=1.f;
  uint8_t stencil=0;
};
// Single-main-color/depth native Vulkan backend can clear a source RECT
// only if the RECT spans the ENTIRE original output target. Partial clears
// need a separate native scissored/quad implementation, not a guessed clear.
inline bool IsOriginalFullTargetClear(
    const OriginalXdkClearF& clear,uint32_t original_width,
    uint32_t original_height) noexcept {
  if(!original_width || !original_height ||
     (clear.flags & ~0x31u)!=0 || (clear.flags & 0x31u)==0 ||
     ((clear.flags&1u)!=0 && !clear.has_color) ||
     ((clear.flags&0x10u)!=0 &&
      (!std::isfinite(clear.depth) || clear.depth<0.f || clear.depth>1.f)))
    return false;
  if(!clear.has_rect)return true;
  return clear.rect[0]==0 && clear.rect[1]==0 &&
         clear.rect[2]==static_cast<int32_t>(original_width) &&
         clear.rect[3]==static_cast<int32_t>(original_height);
}
std::optional<OriginalXdkClearF> CaptureOriginalXdkClearF(
    const std::array<uint32_t,8>& gpr,double depth_f1,
    ReadGuestBytes reader,void* guest_base) noexcept;
struct RawCommand {
  CommandKind kind=CommandKind::DrawVertices;
  uint32_t function=0, lr=0;
  std::array<uint32_t,8> gpr{};  // original r3..r10
  uint32_t draw_ordinal=UINT32_MAX; // populated only for Draw commands
  std::optional<OriginalXdkClearF> captured_clear;
};
struct OriginalGuestBufferBytes {
  uint32_t guest_object=0,guest_address=0,guest_size=0;
  std::vector<std::byte> bytes;
};
struct RawDraw {
  Kind kind{};
  uint32_t function = 0, lr = 0;
  std::array<uint32_t, 8> gpr{}; // r3..r10 from original USA PPC call.
  // Read BEFORE the guest Draw mutates the XDK device shadow state.
  // A missing snapshot is not a verified camera or constant ABI.
  std::optional<OriginalVsMatrices> observed_vs_matrices;
  // Optional immutable full 256*float4 VS and PS banks. A shared pointer
  // avoids copying 8 KiB of guest data during mailbox/frame handoff.
  // Required for arbitrary original lighting/material constants, separate
  // from the c7..c25 camera/transform reflection.
  std::shared_ptr<const OriginalShaderRegisters> observed_shader_constants;
  std::shared_ptr<const OriginalGuestBufferBytes> source_vb0;
  std::shared_ptr<const OriginalGuestBufferBytes> source_ib;
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
// Original USA frontend DrawVertices prim=6 (D3DPT_TRIANGLEFAN) emits two
// triangles for a four-vertex quad; Vulkan's native DrawIndexed uses lists.
// Topology conversion is independent of archive/game resource ownership.
inline std::optional<uint32_t> VerifiedTriangleListCount(
    uint32_t primitive,uint32_t count) noexcept {
  if(primitive==4u && count>=3u && count%3u==0)return count;
  if(primitive==6u && count>=3u && count-2u<=UINT32_MAX/3u)
    return 3u*(count-2u);
  return std::nullopt;
}
inline uint32_t OriginalTriangleFanVertex(
    uint32_t triangle,uint32_t corner) noexcept {
  return corner==0u ? 0u : triangle+corner;
}
struct Frame {
  uint64_t sequence = 0;
  uint32_t dropped = 0;
  std::vector<RawDraw> calls;
  // Explicit interleaved original XDK command order, optional only for
  // historical diagnostics. Required for future independent presentation.
  std::vector<RawCommand> commands;
  uint32_t dropped_commands = 0;
};
// A complete native frame must include EXACTLY the recorded Draw subsequence
// and one terminal Present; discarded commands invalidate the full scene.
bool ValidateOriginalCommandTimeline(const Frame& original) noexcept;
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
  // Immutable preview outlives the mailbox lock, allowing Vulkan source
  // eligibility checks without blocking the guest PPC Present thread.
  // Drop all previews before calling TakeIfSequence.
  std::shared_ptr<const Frame> Peek() const noexcept {return pending_;}
  uint64_t MissedSinceTake() const noexcept {return missed_since_take_;}
  // Atomically reject a stale frame after another original guest Present.
  std::optional<DeliveredFrame> TakeIfSequence(uint64_t source_sequence);
  uint64_t TotalOverwritten() const noexcept {return total_overwritten_;}
  void Reset() noexcept;
 private:
  std::shared_ptr<Frame> pending_;
  uint64_t total_overwritten_=0;
  uint64_t missed_since_take_=0;
};
class FrameCapture {
 public:
  static constexpr std::size_t kMaxDraws = 8192;
  static constexpr std::size_t kMaxCommands = 65536;
  void Record(const RawDraw& call);
  void RecordCommand(const RawCommand& command);
  Frame Finish();
  void Reset();
 private:
  uint64_t seq_ = 0;
  uint32_t dropped_ = 0;
  uint32_t dropped_commands_ = 0;
  std::vector<RawDraw> calls_;
  std::vector<RawCommand> commands_;
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
