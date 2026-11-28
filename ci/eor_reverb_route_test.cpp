#include "../native_port/src/eor_reverb_route.h"
#include <cassert>
#include <limits>
using namespace hulk::eor::audio;
int main(){
  OriginalFmodReverbSends sends;
  assert(ResolveNativeReverbSend(sends)==0.f);
  sends.count=2;sends.values[0]={1,.25f,true};sends.values[1]={2,.5f,false};
  assert(ResolveNativeReverbSend(sends)==.75f);
  auto m=NativeWetMatrix(2,.75f);assert(m&&(*m)[0]==.375f&&(*m)[1]==.375f);
  m=NativeWetMatrix(1,.75f);assert(m&&(*m)[0]==.75f&&(*m)[1]==0.f);
  sends.values[1].gain=1.f;assert(ResolveNativeReverbSend(sends)==1.f);
  sends.values[1].owner=1;assert(!ResolveNativeReverbSend(sends));
  sends.values[1].owner=2;sends.values[1].gain=std::numeric_limits<float>::quiet_NaN();
  assert(!ResolveNativeReverbSend(sends));
  sends.count=65;assert(!ResolveNativeReverbSend(sends));
  assert(!NativeWetMatrix(3,.5f)&&!NativeWetMatrix(2,-1.f));
}
