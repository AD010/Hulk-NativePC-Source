#include "../native_port/src/eor_original_havok_catchup.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace hulk::eor::havok;
static void check(bool v){if(!v)std::abort();}
int main(){
  // Original 82611874 fdivs followed by BL to 82738B58:
  // the host may recover debt only after all pre-step callbacks, at
  // the matching source LR and the actual single-precision quotient.
  const float slow=kOriginalStep*7.25f;
  const double quotient=static_cast<double>(slow/kOriginalStep);
  check(MatchesOriginalSchedulerFloorInput(kOriginalSchedulerFloorReturn,
        quotient,slow,kOriginalStep));
  check(!MatchesOriginalSchedulerFloorInput(0x82611878u,quotient,slow,kOriginalStep));
  check(!MatchesOriginalSchedulerFloorInput(kOriginalSchedulerFloorReturn,
        quotient+1,slow,kOriginalStep));
  check(!MatchesOriginalSchedulerFloorInput(kOriginalSchedulerFloorReturn,
        quotient,slow,1.0f/30));
  for(unsigned fps=30;fps<=240;++fps) {
    const float dt=1.0f/float(fps);
    auto p=PlanOriginalCatchup(dt,kOriginalStep,0,0);
    check(p && !p->extra && !p->debt && p->accumulator==0);
  }
  auto p=PlanOriginalCatchup(kOriginalStep*7,kOriginalStep,0,0);
  check(p && p->extra==1 && p->debt==3);
  p=PlanOriginalCatchup(kOriginalStep*12,kOriginalStep,0,0);
  check(p && p->extra==1 && p->debt==8);
  p=PlanOriginalCatchup(kOriginalStep,kOriginalStep,0,p->debt);
  check(p && p->extra==3 && p->debt==5);
  p=PlanOriginalCatchup(kOriginalStep,kOriginalStep,0,p->debt);
  check(p && p->extra==3 && p->debt==2);
  p=PlanOriginalCatchup(kOriginalStep,kOriginalStep,0,p->debt);
  check(p && p->extra==2 && !p->debt);
  for(uint32_t debt=0;debt<=100;++debt)
    for(unsigned ticks=1;ticks<=14;++ticks) {
      const float elapsed=kOriginalStep*(float(ticks)+0.25f);
      const float residual=kOriginalStep*0.4f;
      p=PlanOriginalCatchup(elapsed,kOriginalStep,residual,debt);
      check(bool(p));
      const auto base=std::min(ticks,3u);
      check(base+p->extra+1<=kCatchupMaxTotalSteps);
      check(p->debt+p->extra+base==debt+ticks);
      check(p->accumulator>=residual);
    }
  // Repeated destruction stalls followed by high-refresh frames must
  // preserve all whole-step debt within the supported recovery window.
  for(unsigned fps:{60u,90u,120u,144u,165u,240u}) {
    uint32_t debt=0,total_requested=0,total_admitted=0;
    for(unsigned frame=0;frame<600;++frame) {
      const float elapsed=frame%30==0?kOriginalStep*12.25f:1.0f/float(fps);
      const auto whole=static_cast<uint32_t>(std::floor(elapsed/kOriginalStep));
      const auto base=std::min(whole,3u);
      const auto recovery=PlanOriginalCatchup(elapsed,kOriginalStep,0,debt);
      check(bool(recovery));
      total_requested+=whole;
      total_admitted+=base+recovery->extra;
      debt=recovery->debt;
      check(total_admitted+debt==total_requested);
      check(base+recovery->extra+1<=kCatchupMaxTotalSteps);
    }
    check(debt==0);
  }
  check(!PlanOriginalCatchup(0,kOriginalStep,0,1));
  check(!PlanOriginalCatchup(1,kOriginalStep,0,0));
  check(!PlanOriginalCatchup(0.1f,1.0f/30,0,0));
  check(!PlanOriginalCatchup(0.1f,kOriginalStep,kOriginalStep,0));
  check(!PlanOriginalCatchup(0.1f,kOriginalStep,-1,0));
  check(!PlanOriginalCatchup(std::numeric_limits<float>::quiet_NaN(),kOriginalStep,0,0));
  std::cout<<"Original 60Hz scheduler catch-up arithmetic PASS\n";
}
