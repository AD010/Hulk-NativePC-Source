#include "../native_port/src/eor_fev_routes.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <array>
using namespace hulk::eor::audio;
int main(int argc,char** argv){
  FevEventEntry event;event.parameters.resize(2);
  FevEffectRecord fx;fx.control=UINT32_MAX;fx.header={0,0x0c};fx.tail={1,0};
  auto route=ReadFevCurveRoute(event,fx);assert(route&&route->target==FevTarget::Volume&&route->parameter==1);
  FevGainPitch result;assert(AccumulateFevGainPitch(result,route->target,.5f)&&result.gain==.5f);
  fx.header[1]=0x14;route=ReadFevCurveRoute(event,fx);assert(route&&route->target==FevTarget::Pitch);
  assert(AccumulateFevGainPitch(result,route->target,.625f)&&result.ratio==2.f);
  assert(AccumulateFevGainPitch(result,route->target,.375f)&&result.ratio==1.f);
  assert(AccumulateFevGainPitch(result,FevTarget::Pitch,0.f)&&result.ratio==.0625f);
  assert(AccumulateFevGainPitch(result,FevTarget::Pitch,1.f)&&result.ratio==1.f);
  assert(!AccumulateFevGainPitch(result,FevTarget::Pan,.5f));
  fx.tail[0]=2;assert(!ReadFevCurveRoute(event,fx));fx.tail[0]=0;
  fx.header[1]=0x44;assert(ReadFevCurveRoute(event,fx)->target==FevTarget::TimeOffset);
  fx.control=5;fx.header={1,4};assert(ReadFevCurveRoute(event,fx)->instance==5);
  if(argc==1)return 0;
  assert(argc==2);std::ifstream f(argv[1],std::ios::binary);assert(f);
  std::array<unsigned,7> counts{};unsigned projects=0;unsigned high=0,low=0,volume=0;
  for(;;){uint32_t h[4];if(!f.read(reinterpret_cast<char*>(h),16)){assert(f.eof()&&!f.gcount());break;}
    std::vector<std::byte>b(h[3]);assert(f.read(reinterpret_cast<char*>(b.data()),b.size()));if(!h[0])continue;
    auto t=AudioText(b);auto e=t.find('\0');auto a=InspectFevEventTable(AudioBytes(b).subspan(e+1),t.substr(0,e));assert(a);++projects;
    for(const auto& ev:a->events)for(const auto& l:ev.layer_records)for(const auto& x:l.effects){
      auto r=ReadFevCurveRoute(ev,x);assert(r);assert(r->parameter==x.tail[0]);++counts[static_cast<unsigned>(r->target)];
      if(r->target==FevTarget::InstanceControl){
        const auto* target=ResolveFevLinkedEnvelope(l,size_t(&x-l.effects.data()));assert(target);
        if(target->name=="FMOD Highpass")++high;
        else if(target->name=="FMOD Lowpass")++low;
        else {assert(target->name.empty()&&target->header[1]==0x0c);++volume;}
      }
    }
  }
  assert(projects==744);assert((counts==std::array<unsigned,7>{1585,149,1216,800,34,83,448}));
  assert(high==119&&low==28&&volume==2);
  std::cout<<"Linked: "<<high<<" Highpass, "<<low<<" Lowpass, "<<volume<<" Volume without DSP handle\n";
  std::cout<<projects<<" projects; named, instance, volume, pitch, pan, time offset, 3D pan level:";
  for(auto n:counts)std::cout<<' '<<n;
  std::cout<<'\n';
}
