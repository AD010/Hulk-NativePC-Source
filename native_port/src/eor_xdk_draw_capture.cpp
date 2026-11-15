#include "eor_xdk_draw_capture.h"
#include <stdexcept>

namespace hulk::eor::xdk {
void FrameCapture::Record(const RawDraw& call) {
  if (!call.function) throw std::invalid_argument("Missing verified XDK address");
  if (calls_.size() >= kMaxDraws) {
    ++dropped_;
    return;
  }
  calls_.push_back(call);
}
Frame FrameCapture::Finish() {
  Frame out;
  out.sequence = ++seq_;
  out.dropped = dropped_;
  out.calls.swap(calls_);
  dropped_ = 0;
  return out;
}
void FrameCapture::Reset() {
  calls_.clear();
  dropped_ = 0;
}
}  // namespace hulk::eor::xdk
