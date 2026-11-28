#include "../native_port/src/eor_fmod_capture_schedule.h"
#include <cstdlib>
using namespace hulk::eor::audio;
using namespace std::chrono_literals;
static void Check(bool b){if(!b)std::abort();}
int main(){
  FmodCaptureSchedule s;
  const FmodCaptureSchedule::Clock::time_point now{};
  Check(s.Begin(now,false));Check(!s.Begin(now+16ms,false));
  s.WaveStarted();Check(!s.Begin(now+17ms,false));Check(s.Begin(now+100ms,false));
  s.Reset();Check(s.Begin(now,true));Check(!s.Begin(now+1ms,true));
  s.WaveStarted();s.WaveStarted();Check(s.Begin(now+2ms,true));
  Check(!s.Begin(now+2ms,true));Check(!s.Begin(now+17ms,true));Check(s.Begin(now+18ms,true));
  Check(s.Begin(now,true)); // clock discontinuity
  s.Reset();Check(s.Begin(now,true));Check(FmodCaptureSchedule::ShadowStartBudget()==4);
}
