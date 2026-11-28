#include "../native_port/src/eor_static_dsp.h"
#include <cassert>
#include <limits>
#include <vector>
using namespace hulk::eor::audio;
static std::vector<int16_t> Tone(double hz){
  std::vector<int16_t> v(4800);
  for(size_t i=0;i<v.size();++i)v[i]=static_cast<int16_t>(12000*std::sin(2*std::numbers::pi*hz*i/48000));
  return v;
}
static double Energy(const std::vector<int16_t>& v){double sum=0;for(size_t i=1200;i<v.size();++i)sum+=double(v[i])*v[i];return sum;}
int main(){
  assert(OriginalUsaDspRange(DspKind::Highpass,0)->minimum==1.f);
  assert(OriginalUsaDspRange(DspKind::Lowpass,0)->maximum==22000.f);
  assert(OriginalUsaDspRange(DspKind::Lowpass,1)->initial==1.f);
  assert(!OriginalUsaDspRange(DspKind::LowpassSimple,1));
  const float a=OriginalUsaSimpleLowpassAlpha(1000.f,48000);
  assert(a>.115f&&a<.117f);
  assert(OriginalUsaSimpleLowpassAlpha(22000.f,48000)==1.f);
  assert(OriginalUsaSimpleLowpassAlpha(20000.f,48000)>.89f);
  OriginalUsaSimpleLowpassState st;
  assert(std::abs(st.Process(1.f,.5f)-.25f)<1e-7f);
  assert(std::abs(st.Process(0.f,.5f)-.25f)<1e-7f);
  assert(st.Process(.75f,1.f)==.75f&&st.first==.25f&&st.second==.25f);
  assert(st.Process(0.f,.75f)==.109375f);
  assert(st.Process(1.f,0.f)==0.f&&st.first==0.f&&st.second==0.f);
  OriginalUsaHighpass hp;
  hp.Configure(1000.f,48000,1.f);
  assert(std::abs(hp.Process(1.f)-hp.b0/hp.a0)<1e-6f);
  assert(hp.x1==1.f);
  hp.Configure(1100.f,48000,1.f);assert(hp.x1==1.f);
  hp.Configure(2100.f,48000,1.f);assert(hp.x1==0.f);
  hp.Configure(22000.f,48000,1.f);assert(hp.Process(.75f)==0.f);
  hp.Configure(1.f,48000,1.f);assert(hp.Process(.75f)==.75f);
  hp.Configure(22000.f,8000,1.f);assert(hp.cutoff==3990.f);
  OriginalUsaLowpass lp;lp.Configure(1000.f,48000,1.f);
  assert(lp.section[0].a1!=lp.section[1].a1);
  assert(lp.Process(1.f)==lp.gain);
  for(int i=0;i<10000;++i)assert(std::isfinite(lp.Process(0.f)));
  OriginalUsaLowpass dc;dc.Configure(1000.f,48000,1.f);
  float steady=0;for(int i=0;i<10000;++i)steady=dc.Process(.25f);
  assert(std::abs(steady-.25f)<.0001f);
  StaticDspChain c;auto v=Tone(500),old=v;assert(ApplyStaticDsp(v,48000,1,c)&&v==old);
  c.count=1;c.stages[0]={DspKind::Distortion,0,1};assert(ApplyStaticDsp(v,48000,1,c)&&v==old);
  c.stages[0].value=.5f;assert(ApplyStaticDsp(v,48000,1,c)&&v!=old);
  assert(OriginalUsaDistortion(.25f,.5f)==.5f);
  assert(OriginalUsaDistortion(-.5f,.5f)==-.75f);
  assert(OriginalUsaDistortion(1.f,1.f)==1.f);
  assert(OriginalUsaDistortion(0.f,1.f)==0.f);
  std::vector<int16_t> exact{8192,-16384,0};
  assert(ApplyStaticDsp(exact,48000,1,c));
  assert(exact[0]==16384&&exact[1]==-24576&&exact[2]==0);
  for(auto k:{DspKind::Lowpass,DspKind::LowpassSimple,DspKind::Highpass}){
    c.stages[0]={k,1000,k==DspKind::Lowpass?1.f:.70710678f};auto low=Tone(100),high=Tone(10000);
    double el=Energy(low),eh=Energy(high);assert(ApplyStaticDsp(low,48000,1,c)&&ApplyStaticDsp(high,48000,1,c));
    if(k==DspKind::Highpass){assert(Energy(low)<el*.01&&Energy(high)>eh*.9);}
    else {assert(Energy(low)>el*.9&&Energy(high)<eh*.03);}
  }
  c.stages[0]={DspKind::Lowpass,1000,1.f};std::vector<int16_t> stereo(4800);stereo[0]=20000;
  assert(ApplyStaticDsp(stereo,48000,2,c));for(size_t i=1;i<stereo.size();i+=2)assert(stereo[i]==0);
  c.count=2;c.stages[1]={DspKind::Unknown,0,1};v=old;assert(!ApplyStaticDsp(v,48000,1,c)&&v==old);
  c.count=1;c.stages[0].value=std::numeric_limits<float>::quiet_NaN();assert(!ApplyStaticDsp(v,48000,1,c)&&v==old);
  assert(ClassifyFevDsp("FMOD Lowpass Simple")==DspKind::LowpassSimple&&ClassifyFevDsp("")==DspKind::Unknown);
}
