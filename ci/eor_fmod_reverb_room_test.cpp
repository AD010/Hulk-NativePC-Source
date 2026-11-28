#include "../native_port/src/eor_reverb_route.h"
#include <cassert>
#include <map>
using namespace hulk::eor::audio;
int main(){
  std::map<uint32_t,std::byte> mem;
  constexpr uint32_t p=0x1000;
  auto put=[&](uint32_t o,uint32_t v){for(unsigned i=0;i<4;++i)mem[p+o+i]=std::byte(v>>(24-8*i));};
  auto f=[&](uint32_t o,float v){put(o,std::bit_cast<uint32_t>(v));};
  auto read=[&](uint32_t a,std::byte* d,size_t n){for(size_t i=0;i<n;++i){auto it=mem.find(a+static_cast<uint32_t>(i));if(it==mem.end())return false;d[i]=it->second;}return true;};
  put(40,std::bit_cast<uint32_t>(int32_t(-900)));put(44,std::bit_cast<uint32_t>(int32_t(-200)));
  put(64,std::bit_cast<uint32_t>(int32_t(-1000)));put(84,200);
  f(132,.5f);f(52,2.f);f(56,.8f);f(68,.01f);f(88,.02f);f(136,70.f);f(140,80.f);f(124,5000.f);
  auto room=ReadOriginalFmodReverbRoom(p,read);assert(room&&room->room==-900&&room->room_hf==-200&&room->reflections==-1000&&room->decay==2.f&&room->reverb_delay==.02f&&room->rolloff==.5f&&room->density==80.f);
  f(52,0.f);assert(!ReadOriginalFmodReverbRoom(p,read));f(52,2.f);
  mem.erase(p+124);assert(!ReadOriginalFmodReverbRoom(p,read));
  OriginalFmodReverbSends sends;sends.count=3;sends.values[0]={5,.5f,true,room};
  sends.values[1]={2,.5f,false,room};sends.values[2]={7,1.f,false,{}};
  NativeReverbRoomChoice choice;choice.Consider(sends);assert(choice.owner==2&&choice.room==room);
  sends.values[2].room=room;choice.Consider(sends);assert(choice.owner==7&&choice.gain==1.f);
  sends.values[2].owner=2;NativeReverbRoomChoice invalid;invalid.Consider(sends);assert(!invalid.room);
}
