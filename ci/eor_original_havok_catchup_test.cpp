#include "../native_port/src/eor_original_havok_catchup.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace hulk::eor::havok;
static void check(bool v){if(!v)std::abort();}
int main(){
  for(unsigned fps=30;fps<=240;++fps) {
    const float dt=1.0f/float(fps);
    auto p=PlanOriginalCatchup(dt,kOriginalStep,0,0);
    check(p && !p->extra && !p->debt && p->accumulator==0);
  }
  auto p=PlanOriginalCatchup(kOriginalStep*7,kOriginalStep,0,0);
  check(p && p->extra==4 && !p->debt);
  p=PlanOriginalCatchup(kOriginalStep*12,kOriginalStep,0,0);
  check(p && p->extra==4 && p->debt==5);
  p=PlanOriginalCatchup(kOriginalStep,kOriginalStep,0,p->debt);
  check(p && p->extra==5 && !p->debt);
  for(uint32_t debt=0;debt<=100;++debt)
    for(unsigned ticks=1;ticks<=14;++ticks) {
      const float elapsed=kOriginalStep*(float(ticks)+0.25f);
      const float residual=kOriginalStep*0.4f;
      p=PlanOriginalCatchup(elapsed,kOriginalStep,residual,debt);
      check(bool(p));
      const auto base=std::min(ticks,3u);
      check(base+p->extra+1<=8);
      check(p->debt+p->extra+base==debt+ticks);
      check(p->accumulator>=residual);
    }
  check(!PlanOriginalCatchup(0,kOriginalStep,0,1));
  check(!PlanOriginalCatchup(1,kOriginalStep,0,0));
  check(!PlanOriginalCatchup(0.1f,1.0f/30,0,0));
  check(!PlanOriginalCatchup(0.1f,kOriginalStep,kOriginalStep,0));
  check(!PlanOriginalCatchup(0.1f,kOriginalStep,-1,0));
  check(!PlanOriginalCatchup(std::numeric_limits<float>::quiet_NaN(),kOriginalStep,0,0));
  std::cout<<"Original 60Hz scheduler catch-up arithmetic PASS\n";
}
