#include "../native_port/src/eor_spatial_audio.h"
#include "../native_port/src/eor_fev_voice_schedule.h"
#include <cassert>
#include <limits>
using namespace hulk::eor::audio;
int main(){
  SpatialVoiceState state;assert(ValidSpatialState(state));
  state.emitter.position={1,2,3};assert(ValidSpatialState(state));
  state.listener.up={0,0,1};assert(!ValidSpatialState(state));state.listener.up={0,1,0};
  state.distance_scale=0;assert(!ValidSpatialState(state));state.distance_scale=1;
  state.emitter.position.x=std::numeric_limits<float>::infinity();assert(!ValidSpatialState(state));
  const std::array<float,2> a{.5f,.5f},b{0,1};std::array<float,2> out{};
  assert(BlendSpatialMatrix(a,b,0,out)&&out==a);
  assert(BlendSpatialMatrix(a,b,1,out)&&out==b);
  assert(BlendSpatialMatrix(a,b,.5f,out)&&out[0]==.25f&&out[1]==.75f);
  auto before=out;assert(!BlendSpatialMatrix(a,b,-1,out)&&out==before);
  std::array<std::byte,24> fields{};fields[11]=std::byte{0x3f};fields[10]=std::byte{0x80};
  FevEventEntry event;event.parameters.push_back({"test",fields,{}});event.layer_records.resize(1);
  FevEffectRecord fx;fx.control=UINT32_MAX;fx.header={0,0x404};fx.points={{{0,0,2}},{{0x3f800000,0x3f800000,2}}};
  event.layer_records[0].effects.push_back(fx);
  std::array<float,1> x{0},y{1};std::array<FevParameterSnapshot,2> snapshots{{{0,x},{20,y}}};
  assert(!BuildFevVoiceSchedule(event,0,snapshots));
  const auto schedule=BuildFevVoiceSchedule(event,0,snapshots,true);
  assert(schedule&&schedule->size()==2&&schedule->front().controls.pan3d_level==0.f&&schedule->back().controls.pan3d_level==1.f);
  assert(!ValidFevVoiceSchedule(*schedule,64,1)&&ValidFevVoiceSchedule(*schedule,64,1,true));
  assert(!ResolveFevFixedGainPitch(event,0,snapshots));
}
