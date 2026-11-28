#include "../native_port/src/eor_fmod_update_clock.h"
#include <cassert>
#include <map>
using namespace hulk::eor::audio;
int main(){
  std::map<uint32_t,uint32_t> words{{0x1000,kUsaEventSystemVtable},{kUsaEventSystemVtable+8,kUsaEventSystemUpdate},{0x1000+160,1},{0x1000+28,100}};
  auto read=[&](uint32_t a,std::byte* dst,size_t n){auto it=words.find(a);if(n!=4||it==words.end())return false;for(unsigned i=0;i<4;++i)dst[i]=std::byte(it->second>>(24-i*8));return true;};
  const auto before=CaptureFmodUpdateStamp(0x1000,read);assert(before&&before->clock==100);
  words[0x1000+28]=116;const auto after=CaptureFmodUpdateStamp(0x1000,read);assert(after&&FmodUpdateDelta(*before,*after,0)==16u);
  assert(!FmodUpdateDelta(*before,*after,1));assert(!FmodUpdateDelta(*before,{0x2000,116},0));
  assert(FmodUpdateDelta({0x1000,UINT32_MAX},{0x1000,4},0)==4u);
  assert(FmodUpdateDelta({0x1000,0},*after,0)==0u);
  words[0x1000+160]=0;assert(!CaptureFmodUpdateStamp(0x1000,read));words[0x1000+160]=1;
  words[0x1000]=0;assert(!CaptureFmodUpdateStamp(0x1000,read));
  assert(!CaptureFmodUpdateStamp(UINT32_MAX,read));assert(!VerifyFmodUpdateBody(read));
}
