#include "../native_port/src/eor_voice_options.h"
#include <cassert>
#include <limits>
using namespace hulk::eor::audio;
int main(){
  VoiceOptions v;assert(ValidVoiceOptions(v,64));assert(!ValidVoiceOptions(v,0));
  v.loop_count=255;v.loop_begin=32;v.loop_length=32;assert(ValidVoiceOptions(v,64));
  v.loop_length=33;assert(!ValidVoiceOptions(v,64));v.loop_length=32;
  v.frequency_ratio=std::numeric_limits<float>::quiet_NaN();assert(!ValidVoiceOptions(v,64));v.frequency_ratio=2;
  v.filter=VoiceFilter::HighPass;v.filter_frequency=.5f;assert(ValidVoiceOptions(v,64));
  v.filter_one_over_q=0;assert(!ValidVoiceOptions(v,64));v.filter_one_over_q=1;
  v.delay_ms=600001;assert(!ValidVoiceOptions(v,64));
  PendingVoiceClock c{100,true,false};assert(!c.Advance(40)&&c.remaining_ms==60);
  c.paused=true;assert(!c.Advance(1000)&&c.remaining_ms==60);c.paused=false;
  assert(!c.Advance(59)&&c.remaining_ms==1);assert(c.Advance(1)&&c.remaining_ms==0);
  c.pending=false;assert(!c.Advance(1));
}
