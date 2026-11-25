#pragma once
// EoR/Havok fixed-step transition contract: solver invocation needs verified USA ABI.
#include <cstdint>
#include <functional>
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
// A usable native Havok 60Hz step scheduler with transactional callback
// dispatch. Source-proven ORIGINAL hkpWorld and stepDeltaTime ABIs are required
// before enabling this in Hulk USA; a generic packfile is not such proof.
struct VerifiedHavokRuntime {
  bool original_world_identity_verified=false;
  bool original_step_function_abi_verified=false;
  bool original_world_units_verified=false;
  bool original_job_completion_barrier_verified=false;
};
class NativeHavokTickRunner {
 public:
  using OriginalWorldStep=std::function<bool(double fixed_dt_seconds)>;
  NativeHavokTickRunner(VerifiedHavokRuntime proof,OriginalWorldStep step,
                        double physics_hz=60.0,uint32_t per_batch_budget=8);
  StepBatch AdvanceAndRun(double real_elapsed_seconds);
  StepBatch CatchUpAndRun();
  void ResetAfterOriginalWorldReset() noexcept;
  bool Faulted() const noexcept {return faulted_;}
  uint64_t CompletedTicks() const noexcept {return total_ticks_;}
  double QueuedSeconds() const noexcept {return pending_;}
 private:
  StepBatch Run();
  OriginalWorldStep step_world_;
  double fixed_dt_=1.0/60.0;
  double pending_=0.0;
  uint32_t budget_=8;
  uint64_t total_ticks_=0;
  bool faulted_=false;
  // A world-step callback may synchronously run game code. Never permit
  // recursive ticks or reset the clock while original Havok jobs are active.
  bool dispatching_=false;
};
} // namespace hulk::eor::timing
