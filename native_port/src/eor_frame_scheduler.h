#pragma once
// EoR/Havok fixed-step transition contract: solver invocation needs verified USA ABI.
#include <cstdint>
namespace hulk::eor::timing {
struct StepBatch {
  uint32_t simulation_steps = 0;
  double fixed_step_seconds = 0.0;
  double interpolation_alpha = 0.0;
  double queued_seconds = 0.0;
  bool must_catch_up_before_present = false;
};
class FixedStepScheduler {
 public:
  explicit FixedStepScheduler(double physics_hz = 60.0,
                              uint32_t max_steps_per_batch = 8);
  StepBatch Advance(double real_elapsed_seconds);
  StepBatch CatchUp();
  void Reset() noexcept;
  double QueuedSeconds() const noexcept { return accumulated_; }
  uint64_t TotalTicks() const noexcept { return total_ticks_; }
 private:
  StepBatch Drain();
  double step_ = 1.0 / 60.0;
  double accumulated_ = 0.0;
  uint32_t budget_ = 8;
  uint64_t total_ticks_ = 0;
};
} // namespace hulk::eor::timing
