#include "../native_port/src/eor_dynamic_dsp.h"
#include <cassert>
#include <vector>
#include <limits>
using namespace hulk::eor::audio;
int main(){
  for(auto kind:{DspKind::Distortion,DspKind::LowpassSimple,DspKind::Highpass,DspKind::Lowpass}){
    StaticDspChain chain;chain.count=1;
    chain.stages[0]={kind,kind==DspKind::Distortion?.5f:1000.f,1.f};
    DynamicDspProcessor full,split;
    assert(full.Configure(48000,2,chain)&&split.Configure(48000,2,chain));
    std::vector<float> a(1024),b;a[0]=.5f;b=a;
    assert(full.Process(a));
    assert(split.Process(std::span<float>(b).first(256)));
    assert(split.Configure(48000,2,chain));
    assert(split.Process(std::span<float>(b).subspan(256))&&a==b);
    for(size_t i=1;i<a.size();i+=2)assert(a[i]==0.f);
    chain.stages[0].value=kind==DspKind::Distortion?.75f:1100.f;
    assert(split.Configure(48000,2,chain));
    assert(split.Process(b));for(auto v:b)assert(std::isfinite(v));
    chain.stages[0].value=std::numeric_limits<float>::quiet_NaN();
    assert(!split.Configure(48000,2,chain));
  }
  StaticDspChain distortion;distortion.count=1;distortion.stages[0]={DspKind::Distortion,.5f,1.f};
  const std::array<DspParameterBlock,2> blocks{{{0,{}},{2,distortion}}};
  std::vector<int16_t> pcm(8,8192);
  assert(ApplyScheduledDsp(pcm,48000,2,blocks));
  for(size_t i=0;i<pcm.size();++i)assert(pcm[i]==(i<4?8192:16384));
  auto bad=blocks;bad[1].first_frame=0;const auto before=pcm;
  assert(!ApplyScheduledDsp(pcm,48000,2,bad)&&pcm==before);
  bad=blocks;bad[1].chain.stages[0].value=2;
  assert(!ApplyScheduledDsp(pcm,48000,2,bad)&&pcm==before);
  OriginalUsaLowpass lp;lp.Configure(1000,48000,1);
  lp.Process(.5f);lp.Process(.25f);
  const auto old=lp;lp.Configure(1200,48000,1);
  assert(lp.section[0].z1==old.section[0].z1*(lp.section_gain[0]/old.section_gain[0]));
  assert(lp.section[0].z2==old.section[0].z2*(lp.section_gain[1]/old.section_gain[1]));
  assert(lp.section[1].z1==old.section[1].z1&&lp.section[1].z2==old.section[1].z2);
}
