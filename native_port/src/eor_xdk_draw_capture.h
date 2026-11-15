// Actual USA XDK call provenance, NOT a fabricated EoR Draw submission.
// Keep original PPC call-through and memory untouched. Opt-in audit only.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace hulk::eor::xdk {
enum class Kind : uint8_t { DrawVertices, DrawIndexedVertices, DrawVerticesUP };
struct RawDraw {
  Kind kind{};
  uint32_t function = 0, lr = 0;
  std::array<uint32_t, 8> gpr{}; // r3..r10, still UNINTERPRETED PPC arguments.
};
struct Frame {
  uint64_t sequence = 0;
  uint32_t dropped = 0;
  std::vector<RawDraw> calls;
};
class FrameCapture {
 public:
  static constexpr size_t kMaxDraws = 8192;
  void Record(const RawDraw& call);
  Frame Finish();
  void Reset();
 private:
  uint64_t seq_ = 0;
  uint32_t dropped_ = 0;
  std::vector<RawDraw> calls_;
};
// These methods are only called from verified hulk_native_x_profile.ini
// addresses in the AOT USA wrappers. Raw PPC r3..r10 are not host VB/IB
// identities or rasterizable shader programs.
}  // namespace hulk::eor::xdk
