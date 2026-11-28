#include "../native_port/src/eor_fmod_live_events.h"
#include <cassert>
#include <map>
using namespace hulk::eor::audio;
int main(){
  std::map<uint32_t,std::byte> mem;
  auto put=[&](uint32_t a,uint32_t x){for(unsigned i=0;i<4;++i)mem[a+i]=std::byte(x>>(24-8*i));};
  auto str=[&](uint32_t a,const char* s){do{mem[a++]=std::byte(*s);}while(*s++);};
  auto read=[&](uint32_t a,std::byte* p,size_t n){for(size_t i=0;i<n;++i){auto it=mem.find(a+static_cast<uint32_t>(i));if(it==mem.end())return false;p[i]=it->second;}return true;};
  put(0x1000,kUsaEventSystemVtable);put(kUsaEventSystemVtable+8,kUsaEventSystemUpdate);put(0x1000+160,1);put(0x1000+28,7);
  put(0x1000+124,0x2004);put(0x2004,0x1000+124);put(0x2000,0x82089848);put(0x2000+16,0x8000);str(0x8000,"project");
  put(0x2000+64,0x3000+44);put(0x3000+44,0x2000+64);put(0x3000+52,0x3000);
  put(0x3000,0x82089270);put(0x3000+312,0x1000);put(0x3000+68,0x4000);put(0x3000+84,0x8100);str(0x8100,"event");
  put(0x4000,0x820894a8);put(0x4000+44,0x2000);put(0x4000+16,0x8200);str(0x8200,"preload");
  put(0x3000+344,0x5004);put(0x5004,0x3000+344);put(0x5000,0x82089328);put(0x5000+44,0x3000);put(0x5000+16,0x6000);put(0x5000+20,0x3f000000);
  put(0x6000+4,0x8300);str(0x8300,"Intensity");put(0x6000+12,0x40000000);put(0x6000+16,0x40800000);
  auto s=CaptureLiveFmodEvents(0x1000,read);assert(s&&s->size()==1);const auto& e=s->front();
  assert(e.owner==0x3000&&e.project=="project"&&e.group=="preload"&&e.name=="event");
  assert(e.parameters.size()==1&&e.parameters[0].name=="Intensity"&&e.parameters[0].physical==3.f);
  put(0x5000+20,0x7fc00000);assert(!CaptureLiveFmodEvents(0x1000,read));put(0x5000+20,0);
  put(0x4000+44,0x7000);assert(!CaptureLiveFmodEvents(0x1000,read));put(0x4000+44,0x2000);
  put(0x3000+44,0x3000+44);assert(!CaptureLiveFmodEvents(0x1000,read));put(0x3000+44,0x2000+64);
  put(0x2000+64,0x2000+64);s=CaptureLiveFmodEvents(0x1000,read);assert(s&&s->empty());
  put(0x2004,0x2004);assert(!CaptureLiveFmodEvents(0x1000,read));
}
