#include "../native_port/src/eor_fmod_acoustics.h"
#include <cassert>
#include <map>
using namespace hulk::eor::audio;
int main(){
  assert(AdvanceOriginalFmodOcclusion(.2f,.8f,100)==std::fma(100.f,.002f,.2f));
  assert(AdvanceOriginalFmodOcclusion(.8f,.2f,100)==std::fma(-100.f,.002f,.8f));
  assert(AdvanceOriginalFmodOcclusion(.2f,.8f,1000)==.8f);
  assert(AdvanceOriginalFmodOcclusion(.8f,.2f,1000)==.2f);
  assert(AdvanceOriginalFmodOcclusion(.8f,.2f,0)==.8f);
  assert(AdvanceOriginalFmodOcclusion(.8f,.2f,0,true)==.2f);
  assert(!AdvanceOriginalFmodOcclusion(-1.f,.2f,100));
  auto w=EvaluateOriginalFmodZone(5,10,20,true);assert(w&&w->reflected==1&&w->linear==1);
  w=EvaluateOriginalFmodZone(15,10,20,true);assert(w&&std::abs(w->reflected-.0316227766f)<1.e-7f&&w->linear==.5f);
  w=EvaluateOriginalFmodZone(20,10,20,true);assert(w&&w->reflected==0&&w->linear==0);
  w=EvaluateOriginalFmodZone(19.999f,10,20,true);assert(w&&w->reflected>.001f&&w->linear>0);
  w=EvaluateOriginalFmodZone(21,10,20,true);assert(w&&w->reflected==0&&w->linear==0);
  w=EvaluateOriginalFmodZone(21,10,20,false);assert(w&&w->reflected==1&&w->linear==1);
  assert(!EvaluateOriginalFmodZone(1,20,10,true));assert(OriginalFmodZoneSend(.5f,.5f)==.25f);
  std::map<uint32_t,std::byte> mem;
  auto put=[&](uint32_t a,uint32_t v){for(unsigned i=0;i<4;++i)mem[a+i]=std::byte(v>>(24-8*i));};
  auto f=[&](uint32_t a,float v){put(a,std::bit_cast<uint32_t>(v));};
  auto read=[&](uint32_t a,std::byte* dst,size_t n){for(size_t i=0;i<n;++i){auto p=mem.find(a+static_cast<uint32_t>(i));if(p==mem.end())return false;dst[i]=p->second;}return true;};
  constexpr uint32_t core=0x10000,c=0x30000,handle=0x20010003,global=core+36172,end=core+36364,local=0x40000;
  f(c+356,.25f);f(c+360,.5f);f(c+364,.75f);f(c+368,1.f);f(c+276,.75f);f(c+280,.5f);
  auto a=ReadOriginalFmodAcoustics(c,read);assert(a&&a->direct_gain==.75f&&a->reflected==.5f&&a->target_direct==.75f);
  f(c+276,.5f);assert(!ReadOriginalFmodAcoustics(c,read));f(c+276,.75f);
  put(c+40,core);put(c+112,handle);put(c+28,1);put(core+332,2);
  put(global+12,core);put(global+16,0x60000);put(global+152,0x50000);f(0x50004,.25f);
  put(end,local);put(local,end);put(local+12,core);put(local+16,0x70000);put(local+152,0x51000);f(0x51004,.75f);
  auto sends=ReadOriginalFmodReverbSends(core,c,handle,read);
  assert(sends&&sends->count==2&&sends->values[0].global&&sends->values[0].gain==.25f&&sends->values[1].gain==.75f);
  put(local,local);assert(!ReadOriginalFmodReverbSends(core,c,handle,read));put(local,end);
  put(local+12,0);assert(!ReadOriginalFmodReverbSends(core,c,handle,read));put(local+12,core);
  put(local+16,0);assert(ReadOriginalFmodReverbSends(core,c,handle,read)->count==1);
  assert(!ReadOriginalFmodReverbSends(core,c,handle+1,read));
  put(c+28,2);assert(!ReadOriginalFmodReverbSends(core,c,handle,read));
}
