#include "eor_frame_scheduler.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace hulk::eor::timing {
namespace {
constexpr double kEpsilon = 1e-10;
double CheckedStep(double hz, uint32_t budget) {
  if (!std::isfinite(hz) || hz < 15.0 || hz > 240.0 ||
      budget == 0 || budget > 1024)
    throw std::invalid_argument("Invalid Havok fixed-step contract");
  return 1.0 / hz;
}
StepBatch DescribeBatch(uint32_t steps, double dt, double queued) {
  const bool behind = queued + kEpsilon >= dt;
  return {steps, dt, behind ? 0.0 : queued / dt, queued, behind};
}
} // namespace
FixedStepScheduler::FixedStepScheduler(double hz, uint32_t max_steps)
    : step_(CheckedStep(hz, max_steps)), budget_(max_steps) {}
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
  if (steps > UINT64_MAX - total_ticks_)
    throw std::overflow_error("Native original Havok fixed tick overflow");
  accumulated_ = std::max(0.0, accumulated_ - double(steps) * step_);
  if (accumulated_ < kEpsilon) accumulated_ = 0.0;
  total_ticks_ += steps;
  // Preserve every unexecuted Havok step; no large-dt fast-forward or silent drop.
  // A presenter must skip interpolation while must_catch_up_before_present.
  return DescribeBatch(steps, step_, accumulated_);
}
NativeHavokTickRunner::NativeHavokTickRunner(
    VerifiedHavokRuntime proof,OriginalWorldStep callback,
    double hz,uint32_t budget)
    :step_world_(std::move(callback)),fixed_dt_(CheckedStep(hz,budget)),budget_(budget) {
  if(!proof.original_world_identity_verified ||
     !proof.original_step_function_abi_verified ||
     !proof.original_world_units_verified ||
     !proof.original_job_completion_barrier_verified ||
     !step_world_)
    throw std::invalid_argument("Original Hulk USA Havok world/step ABI is unverified");
}
void NativeHavokTickRunner::ResetAfterOriginalWorldReset() noexcept {
  pending_=0.0;
  total_ticks_=0;
  faulted_=false;
}
StepBatch NativeHavokTickRunner::AdvanceAndRun(double elapsed) {
  if(faulted_ || !std::isfinite(elapsed) || elapsed<0.0 || elapsed>10.0 ||
     pending_>std::numeric_limits<double>::max()-elapsed)
    throw std::invalid_argument("Unusable native Havok clock or elapsed time");
  pending_+=elapsed;
  return Run();
}
StepBatch NativeHavokTickRunner::CatchUpAndRun() {
  if(faulted_)
    throw std::logic_error("Cannot resubmit a partially failed native Havok world step");
  return Run();
}
StepBatch NativeHavokTickRunner::Run() {
  uint32_t dispatched=0;
  while(dispatched<budget_ && pending_+kEpsilon>=fixed_dt_) {
    if(total_ticks_==UINT64_MAX)
      throw std::overflow_error("Native original Havok fixed tick overflow");
    bool completed=false;
    try {completed=step_world_(fixed_dt_);}
    catch (...) {
      faulted_=true;
      throw;
    }
    if(!completed) {
      // Never silently consume an unexecuted physics step or retry a
      // potentially half-updated hkpWorld. Requires original world reset.
      faulted_=true;
      throw std::runtime_error("Source-verified original Havok step did not complete");
    }
    pending_=std::max(0.0,pending_-fixed_dt_);
    if(pending_<kEpsilon)pending_=0.0;
    ++total_ticks_;
    ++dispatched;
  }
  return DescribeBatch(dispatched, fixed_dt_, pending_);
}
} // namespace hulk::eor::timing
