#include "../native_port/src/eor_fmod_live_spatial.h"
#include <cassert>
#include <map>
using namespace hulk::eor::audio;
int main(){
  std::map<uint32_t,std::byte> m;
  auto put=[&](uint32_t a,uint32_t v){for(unsigned i=0;i<4;i++)m[a+i]=std::byte((v>>(24-8*i))&255);};
  auto vec=[&](uint32_t a,float x,float y,float z){put(a,std::bit_cast<uint32_t>(x));put(a+4,std::bit_cast<uint32_t>(y));put(a+8,std::bit_cast<uint32_t>(z));};
  auto read=[&](uint32_t a,std::byte* d,size_t n){for(size_t i=0;i<n;i++){auto p=m.find(a+static_cast<uint32_t>(i));if(p==m.end())return false;d[i]=p->second;}return true;};
  constexpr uint32_t es=0x1000,core=0x10000,array=0x30000,c=array+416,sound=0x40000,handle=0x20010003;
  put(es,kUsaEventSystemVtable);put(es+160,1);put(es+28,10);put(es+16,core);
  put(kUsaEventSystemVtable+8,kUsaEventSystemUpdate);put(kUsaEventSystemVtable+120,0x82844368);
  put(core+34140,2);put(core+20,2);put(core+332,2);put(core+336,array);
  put(c+112,handle);put(c+40,core);put(c+48,sound);put(sound+100,16);
  put(core+33724,1);
  const auto f=[&](uint32_t addr,float v){put(addr,std::bit_cast<uint32_t>(v));};
  f(c+356,0.f);f(c+360,0.f);f(c+364,0.f);f(c+368,0.f);f(c+276,1.f);f(c+280,1.f);
  f(c+284,.4f);f(c+328,.5f);f(c+288,1.75f);f(c+388,.25f);
  f(core+33736,2.f);f(core+33728,3.f);f(core+33732,4.f);
  vec(core+33288,1,2,3);vec(core+33216,4,5,6);vec(core+33252,0,0,1);vec(core+33228,0,1,0);
  vec(c+292,7,8,9);vec(c+304,10,11,12);
  put(c+320,std::bit_cast<uint32_t>(2.f));put(c+324,std::bit_cast<uint32_t>(10.f));
  auto a=CaptureLiveFmodSpatial(es,handle,read);assert(a&&a->position.z==9&&a->velocity.x==10&&a->listener.position.y==2&&a->core_flags==2);
  assert(a->min_distance==2.f&&a->max_distance==10.f);
  auto spatial=ResolveLiveFmodSpatial(*a);assert(spatial&&spatial->emitter.position.z==-9&&spatial->listener.front.z==-1);
  assert(std::abs(spatial->distance_gain-.2f)<1.e-6f);
  f(c+356,.5f);f(c+276,.5f);f(c+280,.5f);
  auto occluded=CaptureLiveFmodSpatial(es,handle,read);assert(occluded&&occluded->acoustics);
  assert(std::abs(ResolveLiveFmodSpatial(*occluded)->distance_gain-.05f)<1.e-6f);
  f(c+276,1.f);assert(!CaptureLiveFmodSpatial(es,handle,read));
  f(c+356,0.f);f(c+280,1.f);
  assert(spatial->doppler_from_source&&spatial->doppler_factor==1.75f);
  assert(a->original&&a->original->global_doppler==2.f&&a->original->distance_factor==3.f&&a->original->global_rolloff==4.f&&a->original->channel_doppler==.25f);
  auto raw=*a;raw.mode|=0x04000000u;assert(ResolveLiveFmodSpatial(raw)->distance_gain==.2f);
  f(c+288,0.f);assert(!CaptureLiveFmodSpatial(es,handle,read));f(c+288,1.75f);
  put(core+33724,2);assert(!CaptureLiveFmodSpatial(es,handle,read));put(core+33724,1);
  put(c+40,0);assert(!CaptureLiveFmodSpatial(es,handle,read));put(c+40,core);
  raw=*a;raw.original.reset();raw.core_flags=0;raw.mode=16|0x00040000u|0x00200000u;raw.position={0,0,6};
  spatial=ResolveLiveFmodSpatial(raw);assert(spatial&&spatial->listener.position.z==0&&spatial->emitter.position.z==6&&spatial->distance_gain==.5f);
  raw.position={0,0,1};assert(ResolveLiveFmodSpatial(raw)->distance_gain==1.f);
  raw.position={0,0,20};assert(ResolveLiveFmodSpatial(raw)->distance_gain==0.f);
  raw.mode|=0x04000000u;assert(!ResolveLiveFmodSpatial(raw));
  raw=*a;raw.min_distance=0.f;assert(!ResolveLiveFmodSpatial(raw));
  raw=*a;raw.max_distance=1.f;assert(!ResolveLiveFmodSpatial(raw));
  put(c+324,std::bit_cast<uint32_t>(1.f));assert(!CaptureLiveFmodSpatial(es,handle,read));
  put(c+324,std::bit_cast<uint32_t>(10.f));
  assert(!CaptureLiveFmodSpatial(es,handle+1,read));assert(!CaptureLiveFmodSpatial(es,handle&0xffff0000u,read));
  put(sound+100,0);assert(!CaptureLiveFmodSpatial(es,handle,read));put(sound+100,16);
  vec(core+33252,0,1,0);assert(!CaptureLiveFmodSpatial(es,handle,read));vec(core+33252,0,0,1);
  put(c+292,0x7fc00000u);assert(!CaptureLiveFmodSpatial(es,handle,read));vec(c+292,7,8,9);
  put(core+34140,3);assert(!CaptureLiveFmodSpatial(es,handle,read));put(core+34140,2);
  put(core+336,0xfffffff0u);assert(!CaptureLiveFmodSpatial(es,handle,read));
}
