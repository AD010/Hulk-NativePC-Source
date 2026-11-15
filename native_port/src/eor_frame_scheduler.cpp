#include "eor_frame_scheduler.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace hulk::eor::timing {
namespace { constexpr double kEpsilon = 1e-10; }
FixedStepScheduler::FixedStepScheduler(double hz, uint32_t max_steps)
    : step_(1.0 / hz), budget_(max_steps) {
  if (!std::isfinite(hz) || hz < 15.0 || hz > 240.0 ||
      max_steps == 0 || max_steps > 1024)
    throw std::invalid_argument("Invalid Havok fixed-step contract");
}
void FixedStepScheduler::Reset() noexcept {
  accumulated_ = 0.0;
  total_ticks_ = 0;
}
StepBatch FixedStepScheduler::Advance(double elapsed) {
  if (!std::isfinite(elapsed) || elapsed < 0.0 || elapsed > 10.0)
    throw std::invalid_argument("Invalid real elapsed seconds");
  if (accumulated_ > std::numeric_limits<double>::max() - elapsed)
    throw std::overflow_error("Havok accumulator overflow");
  accumulated_ += elapsed;
  return Drain();
}
StepBatch FixedStepScheduler::CatchUp() { return Drain(); }
StepBatch FixedStepScheduler::Drain() {
  const double available = std::floor((accumulated_ + kEpsilon) / step_);
  const auto steps = static_cast<uint32_t>(std::min<double>(available, budget_));
  accumulated_ = std::max(0.0, accumulated_ - double(steps) * step_);
  if (accumulated_ < kEpsilon) accumulated_ = 0.0;
  total_ticks_ += steps;
  const bool unfinished = accumulated_ + kEpsilon >= step_;
  // Preserve every unexecuted Havok step; no large-dt fast-forward or silent drop.
  // A presenter must skip interpolation while must_catch_up_before_present.
  return {steps, step_, unfinished ? 0.0 : accumulated_ / step_,
          accumulated_, unfinished};
}
} // namespace hulk::eor::timing
