#include "../native_port/src/eor_original_sampler_mips.h"
#undef NDEBUG
#include <cassert>
using namespace hulk::eor::gpu;
int main() {
  auto top=PlanOriginalMipWindow(2048,1024,12,2048,1024);
  assert(top && top->base==0 && top->count==12);
  auto resident=PlanOriginalMipWindow(2048,1024,12,256,128);
  assert(resident && resident->base==3 && resident->count==9);
  auto tail=PlanOriginalMipWindow(2048,1024,12,1,1);
  assert(tail && tail->base==11 && tail->count==1);
  auto odd=PlanOriginalMipWindow(513,257,10,256,128);
  assert(odd && odd->base==1 && odd->count==9);
  assert(!PlanOriginalMipWindow(2048,1024,12,256,256));
  assert(!PlanOriginalMipWindow(2048,1024,3,256,128));
  assert(!PlanOriginalMipWindow(8,8,5,1,1));
  assert(!PlanOriginalMipWindow(0,8,1,1,1));
  auto range=PlanOriginalSamplerMipRange(256,128,9,true,true,0,8,false);
  assert(range && range->lo==0 && range->hi==8);
  range=PlanOriginalSamplerMipRange(256,128,9,false,true,0,8,false);
  assert(range && range->lo==1 && range->hi==8);
  range=PlanOriginalSamplerMipRange(256,128,9,true,true,2,8,true);
  assert(range && range->lo==2 && range->hi==2);
  assert(!PlanOriginalSamplerMipRange(256,128,8,true,true,0,8,false));
  assert(!PlanOriginalSamplerMipRange(256,128,9,false,false,0,0,false));
  assert(!PlanOriginalSamplerMipRange(256,128,9,true,true,16,0,false));
  for(uint32_t w=1;w<=8192;w*=2) {
    for(uint32_t h=1;h<=8192;h*=2) {
      const auto levels=static_cast<uint32_t>(std::bit_width(std::max(w,h)));
      for(uint32_t base=0;base<levels;++base) {
        const auto gw=std::max(1u,w>>base),gh=std::max(1u,h>>base);
        const auto view=PlanOriginalMipWindow(w,h,levels,gw,gh);
        assert(view && view->base==base && view->count==levels-base);
        const auto lod=PlanOriginalSamplerMipRange(gw,gh,view->count,true,true,0,15,false);
        assert(lod && lod->hi==view->count-1);
      }
    }
  }
}
