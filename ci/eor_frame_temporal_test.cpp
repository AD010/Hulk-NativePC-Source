#include "../native_port/src/eor_frame_scheduler.h"
#include "../native_port/src/eor_motion_vectors.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace hulk::eor;
static bool Near(double x,double y,double tolerance=1e-8) {
  return std::abs(x-y)<tolerance;
}
int main() {
  // 30Hz presentation -> 2x60Hz ticks. 120Hz presentation -> half as often.
  timing::FixedStepScheduler thirty(60);
  for(int i=0;i<60;i++) {
    auto b=thirty.Advance(1.0/30.0);
    assert(b.simulation_steps==2 && !b.must_catch_up_before_present);
  }
  assert(thirty.TotalTicks()==120);
  // Step execution is now directly bound to an ORIGINAL world callback
  // when its USA ABI, timestep units and job completion barrier are proven.
  timing::VerifiedHavokRuntime verified{true,true,true,true};
  uint64_t executed=0;
  timing::NativeHavokTickRunner live(verified,[&](double dt){
    assert(Near(dt,1.0/60.0));++executed;return true;
  });
  for(int i=0;i<60;++i) {
    const auto state=live.AdvanceAndRun(1.0/30.0);
    assert(state.simulation_steps==2 &&
           !state.must_catch_up_before_present);
  }
  assert(executed==120 && live.CompletedTicks()==120);
  live.ResetAfterOriginalWorldReset();
  assert(live.CompletedTicks()==0 && Near(live.QueuedSeconds(),0));
  timing::NativeHavokTickRunner highRefresh(verified,[&](double dt){
    assert(Near(dt,1.0/60.0));return true;
  });
  for(int i=0;i<120;++i)
    highRefresh.AdvanceAndRun(1.0/120.0);
  assert(highRefresh.CompletedTicks()==60);
  timing::NativeHavokTickRunner failure(verified,[](double){return false;});
  bool failed_world=false;
  try{failure.AdvanceAndRun(1.0/60.0);}
  catch(const std::runtime_error&){failed_world=true;}
  assert(failed_world && failure.Faulted() && failure.CompletedTicks()==0);
  bool fail_again=false;
  try{failure.CatchUpAndRun();}catch(const std::logic_error&){fail_again=true;}
  assert(fail_again);
  auto no_proof=verified;
  no_proof.original_step_function_abi_verified=false;
  bool missing_abi=false;
  try{timing::NativeHavokTickRunner unavailable(no_proof,
         [](double){return true;});}
  catch(const std::invalid_argument&){missing_abi=true;}
  assert(missing_abi);
  timing::FixedStepScheduler one_twenty(60);
  for(int i=0;i<120;i++) {
    auto b=one_twenty.Advance(1.0/120.0);
    assert(b.simulation_steps<=1 && !b.must_catch_up_before_present);
  }
  assert(one_twenty.TotalTicks()==60);
  timing::FixedStepScheduler stall(60,3);
  auto x=stall.Advance(0.2);
  assert(x.simulation_steps==3 && x.must_catch_up_before_present);
  uint32_t total=x.simulation_steps;
  while(x.must_catch_up_before_present) {
    x=stall.CatchUp(); total+=x.simulation_steps;
  }
  assert(total==12 && stall.TotalTicks()==12 && Near(stall.QueuedSeconds(),0));
  bool thrown=false;
  try { stall.Advance(-1); } catch(const std::invalid_argument&) {thrown=true;}
  assert(thrown);
  temporal::History history;
  temporal::FrameTag tag{100,1,1,2560,1600,true,true,true,false};
  assert(!history.Begin(tag));
  tag.frame=101; assert(history.Begin(tag));
  tag.frame=102;tag.camera_cut=true; assert(!history.Begin(tag));
  tag.frame=103;tag.camera_cut=false; assert(!history.Begin(tag));
  tag.frame=104; assert(history.Begin(tag));
  tag.frame=106; assert(!history.Begin(tag));
  tag.frame=107; assert(history.Begin(tag));
  tag.frame=108;tag.width=1920; assert(!history.Begin(tag));
  tag.frame=109; assert(history.Begin(tag));
  tag.frame=110;tag.scene_depth_verified=false; assert(!history.Begin(tag));
  temporal::Clip prev{0,0,0.5,1}, now{0.25,-0.2,0.5,1};
  auto vel=temporal::FromClip(now,prev,2560,1600);
  assert(vel && Near(vel->x,320) && Near(vel->y,160));
  auto corrected=temporal::FromClip(now,prev,2560,1600,{1,-1},{0,0});
  assert(corrected && Near(corrected->x,319) && Near(corrected->y,161));
  assert(!temporal::FromClip({0,0,0.5,0},prev,2560,1600));
  assert(!temporal::FromClip({0,0,2,1},prev,2560,1600));
  assert(!temporal::FromClip({std::numeric_limits<double>::quiet_NaN(),0,0.5,1},prev,2560,1600));
}
