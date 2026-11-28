#include "../native_port/src/eor_havok_carry.h"
#include <bit>
#include <cstdlib>
#include <limits>
using namespace hulk::eor::havok;
static void Check(bool b){if(!b)std::abort();}
int main(){
  constexpr float step=std::bit_cast<float>(0x3c888889u);
  const float near=std::nextafter(step,0.f);
  const auto baseline=OriginalFractionalCarries(near,near,step,0);
  Check(baseline&&*baseline==1);
  // The old four-extra policy rounds to six carries at this boundary.
  Check(OriginalFractionalCarries(near+4.f*step,near,step,0)==6u);
  const auto fixed=AdmitOriginalCatchup(near,near,step,0,0,4,5);
  Check(fixed&&fixed->extra<4u);
  for(uint32_t whole=0;whole<15;++whole){
    for(float elapsed:{step*float(whole),std::nextafter(step*float(whole+1),0.f),step*(float(whole)+.5f)}){
      const auto original_whole=uint32_t(std::floor(elapsed/step));
      const auto admitted=(std::min)(original_whole,3u);
      for(float remainder:{0.f,step*.5f,near})for(uint32_t requested=0;requested<=4;++requested){
        const auto base=OriginalFractionalCarries(remainder,elapsed,step,original_whole);
        const auto p=AdmitOriginalCatchup(remainder,elapsed,step,original_whole,admitted,requested,5);
        Check(base&&p&&p->extra<=requested);
        const auto actual=OriginalFractionalCarries(p->accumulator,elapsed,step,original_whole);
        Check(actual&&*actual==*base+p->extra&&admitted+*actual<=5);
      }
    }
  }
  Check(!OriginalFractionalCarries(0.f,step,0.f,1));
  Check(!OriginalFractionalCarries(std::numeric_limits<float>::infinity(),step,step,1));
}
