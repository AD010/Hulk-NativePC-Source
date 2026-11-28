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
// FMOD DSPs reconstructed from the USA scalar paths; host rounding differs.
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
         (s.kind==DspKind::Lowpass&&(s.value>=rate*0.49f||s.q<1.f))||s.q<0.1f||s.q>10.f)return false;
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
// USA coefficient generator 0x827A0C68, setter 0x827A1930,
// scalar processing at 0x827A10B8. The legacy resonance is the
// denominator parameter in sin(w)/(2*resonance).
struct OriginalUsaHighpass {
  float b0=0,b1=0,b2=0,a0=1,a1=0,a2=0;
  float x1=0,x2=0,y1=0,y2=0,cutoff=0;
  void Configure(float hz,uint32_t rate,float resonance) noexcept {
    const float previous=cutoff;
    cutoff=(std::min)(hz,float(rate)*.5f-10.f);
    const float w=(cutoff*6.2831854820251465f)/float(rate);
    const float c=std::cos(w),alpha=std::sin(w)/(2.f*resonance);
    b0=(c+1.f)*.5f;b1=-(c+1.f);b2=b0;
    a0=1.f+alpha;a1=-2.f*c;a2=1.f-alpha;
    if(std::abs(previous-cutoff)>=ResetThreshold())x1=x2=y1=y2=0.f;
  }
  static constexpr float ResetThreshold() noexcept {return 1000.f;}
  float Process(float x) noexcept {
    if(cutoff<2.f)return x;
    if(cutoff==22000.f){x1=x2=y1=y2=0.f;return 0.f;}
    float y=std::fma(b0,x,b2*x2);
    y=std::fma(b1,x1,y);
    y=std::fma(-a1,y1,y);y=std::fma(-a2,y2,y)/a0;
    x2=x1;x1=x;y2=y1;y1=y;return y;
  }
};
// USA 0x827A2C80 prewarps the analog prototype; 0x827A2DE8
// applies the bilinear transform. Two sections have distinct damping.
// This is the zero-initial-state, fixed-parameter path only.
struct OriginalUsaLowpass {
  struct Section {float a1=0.f,a2=0.f,z1=0.f,z2=0.f;};
  std::array<Section,2> section{};
  float gain=1.f;
  void Configure(float cutoff,uint32_t rate,float resonance) noexcept {
    const float r=float(rate);
    const float warp=2.f*(std::tan((float(std::atan(1.0))*4.f/r)*cutoff)*r);
    constexpr std::array<float,2> damping{0.7653669714927673f,1.8477590084075928f};
    gain=1.f;
    for(unsigned i=0;i<2;++i){
      const float linear=(damping[i]/resonance)/warp;
      const float quadratic=1.f/(warp*warp);
      const float l=linear,q=quadratic*r;
      const float den=std::fma(std::fma(q,4.f,2.f*l),r,1.f);
      const float inv=1.f/den;
      section[i].a1=std::fma(-8.f,r*q,2.f)*inv;
      section[i].a2=(std::fma(q,4.f,-2.f*l)*r+1.f)*inv;
      gain*=inv;
    }
  }
  float Process(float x) noexcept {
    x*=gain;
    for(auto& s:section){
      const float w=std::fma(-s.a2,s.z2,std::fma(-s.a1,s.z1,x));
      x=(std::fma(2.f,s.z1,s.z2)+w);
      s.z2=s.z1;s.z1=w;
    }
    return x;
  }
};
inline bool ApplyStaticDsp(std::span<int16_t> pcm,uint32_t rate,uint16_t channels,const StaticDspChain& chain) noexcept {
  if((channels!=1&&channels!=2)||pcm.empty()||pcm.size()%channels||!ValidStaticDsp(chain,rate))return false;
  for(unsigned i=0;i<chain.count;++i){
    const auto& s=chain.stages[i];
    if(s.kind==DspKind::Distortion&&s.value==0)continue;
    std::array<OriginalUsaLowpass,2> low{};
    if(s.kind==DspKind::Lowpass)for(auto& f:low)f.Configure(s.value,rate,s.q);
    std::array<OriginalUsaSimpleLowpassState,2> simple{};
    std::array<OriginalUsaHighpass,2> high{};
    if(s.kind==DspKind::Highpass)for(auto& f:high)f.Configure(s.value,rate,s.q);
    const float alpha=s.kind==DspKind::LowpassSimple?OriginalUsaSimpleLowpassAlpha(s.value,rate):0.f;
    for(std::size_t n=0;n<pcm.size();++n){
      const auto ch=n%channels;const double x=pcm[n]/32768.;double y;
      if(s.kind==DspKind::Distortion)y=OriginalUsaDistortion(static_cast<float>(x),s.value);
      else if(s.kind==DspKind::LowpassSimple)y=simple[ch].Process(static_cast<float>(x),alpha);
      else if(s.kind==DspKind::Highpass)y=high[ch].Process(static_cast<float>(x));
      else y=low[ch].Process(static_cast<float>(x));
      pcm[n]=static_cast<int16_t>(std::lround(std::clamp(y*32768.,-32768.,32767.)));
    }
  }
  return true;
}
} // namespace hulk::eor::audio
