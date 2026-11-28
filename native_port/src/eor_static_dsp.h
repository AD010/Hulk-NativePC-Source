#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
namespace hulk::eor::audio {
enum class DspKind : uint8_t { Distortion, LowpassSimple, Highpass, Lowpass, Unknown };
constexpr DspKind ClassifyFevDsp(std::string_view name) noexcept {
  if(name=="FMOD Distortion")return DspKind::Distortion;
  if(name=="FMOD Lowpass Simple")return DspKind::LowpassSimple;
  if(name=="FMOD Highpass")return DspKind::Highpass;
  if(name=="FMOD Lowpass")return DspKind::Lowpass;
  return DspKind::Unknown;
}
// Parameter descriptors from the SHA-pinned USA image (48-byte records).
// Resonance is the legacy FMOD parameter, not necessarily the biquad Q.
struct OriginalDspRange {float minimum,maximum,initial;};
constexpr std::optional<OriginalDspRange> OriginalUsaDspRange(DspKind kind,unsigned parameter) noexcept {
  if(kind==DspKind::Distortion&&parameter==0)return OriginalDspRange{0,1,.5f};
  if(kind==DspKind::LowpassSimple&&parameter==0)return OriginalDspRange{10,22000,5000};
  if(kind==DspKind::Highpass&&parameter==0)return OriginalDspRange{1,22000,5000};
  if(kind==DspKind::Lowpass&&parameter==0)return OriginalDspRange{10,22000,5000};
  if((kind==DspKind::Lowpass||kind==DspKind::Highpass)&&parameter==1)return OriginalDspRange{1,10,1};
  return {};
}
// Explicit physical parameters supplied by a verified caller, NOT normalized
// FEV automation coordinates. These are native approximations to the named
// FMOD DSPs except the USA-verified scalar Distortion transfer function.
struct StaticDspStage {DspKind kind=DspKind::Unknown;float value=0.f,q=0.70710678f;};
struct StaticDspChain {std::array<StaticDspStage,4> stages{};uint8_t count=0;};
inline bool ValidStaticDsp(const StaticDspChain& chain,uint32_t rate) noexcept {
  if(chain.count>chain.stages.size()||rate<4000||rate>96000)return false;
  for(unsigned i=0;i<chain.count;++i){
    const auto& s=chain.stages[i];
    if(!std::isfinite(s.value)||!std::isfinite(s.q))return false;
    if(s.kind==DspKind::Distortion){if(s.value<0||s.value>1)return false;}
    else if(s.kind==DspKind::LowpassSimple||s.kind==DspKind::Highpass||s.kind==DspKind::Lowpass){
      const auto range=OriginalUsaDspRange(s.kind,0);
      if(!range||s.value<range->minimum||s.value>range->maximum||
         (s.kind!=DspKind::LowpassSimple&&s.value>=rate*0.49f)||s.q<0.1f||s.q>10.f)return false;
    }else return false;
  }
  return true;
}
// USA PPC scalar callback body 0x8279EF30 (tail at 0x8279F054).
// Vector reciprocal-estimate rounding is not asserted bit-identical.
inline float OriginalUsaDistortion(float x,float level) noexcept {
  const float k=level<1.f?2.f*level/(1.f-level):19994.681640625f;
  return (x*(1.f+k))/std::fma(std::abs(x),k,1.f);
}
// USA coefficient routine 0x827A1B98 and scalar read 0x827A1D20.
// State is maintained per channel; changing cutoff must not clear it.
inline float OriginalUsaSimpleLowpassAlpha(float cutoff,uint32_t rate) noexcept {
  if(cutoff>=22000.f)return 1.f;
  const float transition=float(rate)*0.31830987334251404f;
  if(cutoff<=transition){
    const float dt=1.f/float(rate);
    return dt/(1.f/(cutoff*6.2831854820251465f)+dt);
  }
  return (cutoff-transition)/((22000.f-transition)*3.f)+0.6666666865348816f;
}
struct OriginalUsaSimpleLowpassState {
  float first=0.f,second=0.f;
  float Process(float x,float alpha) noexcept {
    if(alpha==1.f)return x; // original copy path keeps state unchanged
    if(alpha==0.f){first=second=0.f;return 0.f;}
    const float beta=1.f-alpha;
    first=std::fma(x,alpha,beta*first);
    second=std::fma(beta,second,alpha*first);
    return second;
  }
};
inline bool ApplyStaticDsp(std::span<int16_t> pcm,uint32_t rate,uint16_t channels,const StaticDspChain& chain) noexcept {
  if((channels!=1&&channels!=2)||pcm.empty()||pcm.size()%channels||!ValidStaticDsp(chain,rate))return false;
  for(unsigned i=0;i<chain.count;++i){
    const auto& s=chain.stages[i];
    if(s.kind==DspKind::Distortion&&s.value==0)continue;
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    const double w=2*std::numbers::pi*double(s.value)/rate;
    if(s.kind==DspKind::Highpass||s.kind==DspKind::Lowpass){
      const double c=std::cos(w),alpha=std::sin(w)/(2*s.q),a0=1+alpha;
      a1=-2*c/a0;a2=(1-alpha)/a0;
      if(s.kind==DspKind::Highpass){b0=(1+c)/(2*a0);b1=-2*b0;}
      else {b0=(1-c)/(2*a0);b1=2*b0;}
      b2=b0;
    }
    std::array<double,2> z1{},z2{};
    std::array<OriginalUsaSimpleLowpassState,2> simple{};
    const float alpha=s.kind==DspKind::LowpassSimple?OriginalUsaSimpleLowpassAlpha(s.value,rate):0.f;
    for(std::size_t n=0;n<pcm.size();++n){
      const auto ch=n%channels;const double x=pcm[n]/32768.;double y;
      if(s.kind==DspKind::Distortion)y=OriginalUsaDistortion(static_cast<float>(x),s.value);
      else if(s.kind==DspKind::LowpassSimple)y=simple[ch].Process(static_cast<float>(x),alpha);
      else {y=b0*x+z1[ch];z1[ch]=b1*x-a1*y+z2[ch];z2[ch]=b2*x-a2*y;}
      pcm[n]=static_cast<int16_t>(std::lround(std::clamp(y*32768.,-32768.,32767.)));
    }
  }
  return true;
}
} // namespace hulk::eor::audio
