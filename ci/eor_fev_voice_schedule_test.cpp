#include "../native_port/src/eor_fev_voice_schedule.h"
#include <cassert>
#include <limits>
using namespace hulk::eor::audio;
int main(){
  std::array<std::byte,24> fields{};fields[11]=std::byte{0x3f};fields[10]=std::byte{0x80};
  FevEventEntry event;event.parameters.push_back({"test",fields,{}});event.layer_records.resize(1);
  FevEffectRecord fx;fx.control=UINT32_MAX;fx.header={0,0x0c};fx.points={{{0,0,2}},{{0x3f800000,0x3f800000,2}}};
  event.layer_records[0].effects.push_back(fx);
  fx.header[1]=0x14;fx.points={{{0,0x3f000000,2}},{{0x3f800000,0x3f200000,2}}};
  event.layer_records[0].effects.push_back(fx);
  fx.header[1]=0x24;event.layer_records[0].effects.push_back(fx);
  std::array<float,1> a{0},b{1};
  std::array<FevParameterSnapshot,2> snapshots{{{0,a},{20,b}}};
  auto blocks=BuildFevVoiceSchedule(event,0,snapshots);
  assert(blocks&&blocks->size()==2);
  assert((*blocks)[0].controls.gain==0&&(*blocks)[1].controls.gain==1);
  assert((*blocks)[0].controls.ratio==1&&(*blocks)[1].controls.ratio==2);
  assert((*blocks)[0].controls.pan==0&&(*blocks)[1].controls.pan==-.25f);
  assert(ValidFevVoiceSchedule(*blocks,64,1));
  assert(!ValidFevVoiceSchedule(*blocks,20,1)&&ValidFevVoiceSchedule(*blocks,64,3));
  assert(!ValidFevVoiceSchedule(*blocks,64,9));
  assert(RequiredFevVoiceRatio(*blocks,1)==4.f&&RequiredFevVoiceRatio(*blocks,3)==6.f);
  assert(!RequiredFevVoiceRatio(*blocks,9));
  assert(ValidFevVoiceRatio(.0625f)&&ValidFevVoiceRatio(16.f)&&!ValidFevVoiceRatio(.03f));
  assert(!ValidFevVoiceRatio(17.f));
  assert(FevVoiceBlockAt(*blocks,0)==0&&FevVoiceBlockAt(*blocks,19)==0);
  assert(FevVoiceBlockAt(*blocks,20)==1&&FevVoiceBlockAt(*blocks,UINT64_MAX)==1);
  const auto forward=PlanFevVoiceSeek(*blocks,30,64,false);
  const auto backward=PlanFevVoiceSeek(*blocks,5,64,false);
  assert(forward&&forward->frame==30&&forward->block==1);
  assert(backward&&backward->frame==5&&backward->block==0);
  assert(!PlanFevVoiceSeek(*blocks,64,64,false)&&!PlanFevVoiceSeek(*blocks,5,64,true));
  assert(PlanFevVoiceSeek({},0,1,false)->block==0);
  snapshots[1].first_frame=0;assert(!BuildFevVoiceSchedule(event,0,snapshots));snapshots[1].first_frame=20;
  event.layer_records[0].effects.back().header[1]=0x44;
  assert(!BuildFevVoiceSchedule(event,0,snapshots));
  const auto moving=BuildFevVoiceSchedule(event,0,snapshots,false,true);
  assert(moving&&HasMovingFevOffset(*moving));
  assert(!ValidFevVoiceSchedule(*moving,64,1)&&ValidFevVoiceSchedule(*moving,64,1,false,true));
  FevPlaybackClock clock;
  assert(clock.Observe(21)&&clock.Frame()==21&&FevVoiceBlockAt(*moving,clock.Frame())==1);
  clock.Rebase();assert(clock.Frame()==21&&clock.Observe(0));
  assert(clock.Observe(10)&&clock.Frame()==31&&FevVoiceBlockAt(*moving,clock.Frame())==1);
  assert(!clock.Observe(9)&&clock.Frame()==31);
  FevPlaybackClock overflow{UINT64_MAX,0};assert(!overflow.Observe(1));
  event.layer_records[0].effects.back().header[1]=0x404;
  assert(!BuildFevVoiceSchedule(event,0,snapshots));
  (*blocks)[1].controls.gain=std::numeric_limits<float>::quiet_NaN();
  assert(!ValidFevVoiceSchedule(*blocks,64,1));
}
