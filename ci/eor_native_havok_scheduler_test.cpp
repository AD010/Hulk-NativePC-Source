#include "../native_port/src/eor_frame_scheduler.h"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace hulk::eor::timing;
static void Check(bool ok) { if(!ok)std::abort(); }
static constexpr double kStep=1.0/60.0;
static VerifiedHavokRuntime Proof() {return {true,true,true,true};}
int main() {
  // Only a source-verified original world + ABI + units + finished jobs
  // authorizes native dispatch. No silent substitute solver.
  bool rejected=false;
  try {
    NativeHavokTickRunner invalid({true,true,true,false},[](double){return true;});
  } catch(const std::invalid_argument&) {rejected=true;}
  Check(rejected);
  unsigned calls=0;
  NativeHavokTickRunner world(Proof(),[&](double dt) {
    Check(std::abs(dt-kStep)<1e-12);
    ++calls;
    return true;
  });
  const auto first=world.AdvanceAndRun(1.0/30.0);
  Check(first.simulation_steps==2 && world.CompletedTicks()==2 && calls==2);
  Check(!first.must_catch_up_before_present && !world.Faulted());
  const auto delayed=world.AdvanceAndRun(0.25);
  Check(delayed.simulation_steps==8 && delayed.must_catch_up_before_present);
  Check(world.CompletedTicks()==10);
  while(world.CatchUpAndRun().must_catch_up_before_present) {}
  Check(world.CompletedTicks()==17 && calls==17 && !world.Faulted());

  // A game callback can synchronously invoke the scheduler. Never schedule
  // the same original hkpWorld recursively or append new elapsed time.
  NativeHavokTickRunner* nested_ptr=nullptr;
  bool nested_rejected=false;
  NativeHavokTickRunner nested(Proof(),[&](double) {
    try { (void)nested_ptr->AdvanceAndRun(kStep); }
    catch(const std::logic_error&) {nested_rejected=true;}
    return true;
  });
  nested_ptr=&nested;
  Check(nested.AdvanceAndRun(kStep).simulation_steps==1);
  Check(nested_rejected && nested.CompletedTicks()==1 &&
        nested.QueuedSeconds()<kStep && !nested.Faulted());

  // A world reset while original jobs are in flight is not a completed
  // native physics tick; the clock requires an explicit reset afterwards.
  NativeHavokTickRunner* reset_ptr=nullptr;
  NativeHavokTickRunner reset(Proof(),[&](double) {
    reset_ptr->ResetAfterOriginalWorldReset();
    return true;
  });
  reset_ptr=&reset;
  rejected=false;
  try {(void)reset.AdvanceAndRun(kStep);}
  catch(const std::runtime_error&) {rejected=true;}
  Check(rejected && reset.Faulted() && !reset.CompletedTicks());
  reset.ResetAfterOriginalWorldReset();
  Check(!reset.Faulted() && !reset.CompletedTicks() && reset.QueuedSeconds()==0.0);

  // A failed original job cannot be reissued on a partially modified world.
  NativeHavokTickRunner failed(Proof(),[](double){return false;});
  rejected=false;
  try {(void)failed.AdvanceAndRun(kStep);}
  catch(const std::runtime_error&) {rejected=true;}
  Check(rejected && failed.Faulted() && !failed.CompletedTicks());
  rejected=false;
  try {(void)failed.CatchUpAndRun();}
  catch(const std::logic_error&) {rejected=true;}
  Check(rejected);
  // A competing Windows worker cannot simultaneously change the 60 Hz
  // clock or reset a half-finished hkpWorld step.
  std::promise<void> started,finish;
  auto started_future=started.get_future();
  auto finish_future=finish.get_future().share();
  NativeHavokTickRunner concurrent(Proof(),[&](double) {
    started.set_value();
    finish_future.wait();
    return true;
  });
  std::exception_ptr worker_error;
  std::thread worker([&] {
    try {(void)concurrent.AdvanceAndRun(kStep);}
    catch (...) {worker_error=std::current_exception();}
  });
  started_future.wait();
  bool raced=false;
  try {(void)concurrent.AdvanceAndRun(kStep);}
  catch(const std::logic_error&) {raced=true;}
  Check(raced);
  concurrent.ResetAfterOriginalWorldReset();
  finish.set_value();
  worker.join();
  Check(concurrent.Faulted() && worker_error && !concurrent.CompletedTicks());
  concurrent.ResetAfterOriginalWorldReset();
  Check(!concurrent.Faulted() && concurrent.QueuedSeconds()==0.0);
  std::cout << "Native original Havok 60 Hz runner PASS\n";
}
