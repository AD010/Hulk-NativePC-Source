#include "../native_port/src/eor_fmod_live_mix.h"
#include <cassert>
#include <map>
using namespace hulk::eor::audio;
int main(){
  std::map<uint32_t,std::byte> mem;
  auto put=[&](uint32_t a,uint32_t v){for(unsigned i=0;i<4;++i)mem[a+i]=std::byte(v>>(24-8*i));};
  auto f=[&](uint32_t a,float v){put(a,std::bit_cast<uint32_t>(v));};
  auto read=[&](uint32_t a,std::byte* dst,size_t n){for(size_t i=0;i<n;++i){auto p=mem.find(a+static_cast<uint32_t>(i));if(p==mem.end())return false;dst[i]=p->second;}return true;};
  constexpr uint32_t es=0x1000,core=0x10000,array=0x30000,c=array+416,sound=0x40000,group=0x50000,handle=0x20010003;
  put(es,kUsaEventSystemVtable);put(es+160,1);put(es+28,10);put(es+16,core);put(kUsaEventSystemVtable+8,kUsaEventSystemUpdate);
  put(core+34140,2);put(core+332,2);put(core+336,array);put(c+112,handle);put(c+40,core);put(c+48,sound);put(c+140,group);
  put(sound+100,8);f(c+164,.8f);f(group+60,.5f);
  auto a=CaptureLiveFmodAudibility(es,handle,read);assert(a&&!a->spatial&&a->combined==.4f);
  put(sound+100,16);assert(!CaptureLiveFmodAudibility(es,handle,read));
  f(group+72,.75f);f(c+328,.5f);f(c+284,.25f);f(c+280,.9f);f(c+276,.6f);
  const float expected=(((((.75f*.5f)*.5f)*.25f)*.9f)*.6f)*.8f;
  a=CaptureLiveFmodAudibility(es,handle,read);assert(a&&a->spatial&&a->combined==expected);
  f(c+276,0.f);assert(CaptureLiveFmodAudibility(es,handle,read)->combined==0.f);
  put(c+276,0x7fc00000u);assert(!CaptureLiveFmodAudibility(es,handle,read));f(c+276,.6f);
  assert(!CaptureLiveFmodAudibility(es,handle+1,read));assert(!CaptureLiveFmodAudibility(es,handle&0xffff0000u,read));
  put(c+40,0x20000);assert(!CaptureLiveFmodAudibility(es,handle,read));put(c+40,core);
  f(group+60,-1.f);assert(!CaptureLiveFmodAudibility(es,handle,read));
}
