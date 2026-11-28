#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
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
// Explicit physical parameters supplied by a verified caller, NOT normalized
// FEV automation coordinates. These are native approximations to the named
// FMOD DSPs, not the proprietary FMOD implementation.
struct StaticDspStage {DspKind kind=DspKind::Unknown;float value=0.f,q=0.70710678f;};
struct StaticDspChain {std::array<StaticDspStage,4> stages{};uint8_t count=0;};
inline bool ValidStaticDsp(const StaticDspChain& chain,uint32_t rate) noexcept {
  if(chain.count>chain.stages.size()||rate<4000||rate>96000)return false;
  for(unsigned i=0;i<chain.count;++i){
    const auto& s=chain.stages[i];
    if(!std::isfinite(s.value)||!std::isfinite(s.q))return false;
    if(s.kind==DspKind::Distortion){if(s.value<0||s.value>1)return false;}
    else if(s.kind==DspKind::LowpassSimple||s.kind==DspKind::Highpass||s.kind==DspKind::Lowpass){
      if(s.value<10||s.value>=rate*0.49f||s.q<0.1f||s.q>10.f)return false;
    }else return false;
  }
  return true;
}
inline bool ApplyStaticDsp(std::span<int16_t> pcm,uint32_t rate,uint16_t channels,const StaticDspChain& chain) noexcept {
  if((channels!=1&&channels!=2)||pcm.empty()||pcm.size()%channels||!ValidStaticDsp(chain,rate))return false;
  for(unsigned i=0;i<chain.count;++i){
    const auto& s=chain.stages[i];
    if(s.kind==DspKind::Distortion&&s.value==0)continue;
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    const double w=2*std::numbers::pi*double(s.value)/rate;
    const double drive=1+15*double(s.value),norm=std::tanh(drive);
    if(s.kind==DspKind::LowpassSimple){a1=-std::exp(-w);b0=1+a1;}
    else if(s.kind==DspKind::Highpass||s.kind==DspKind::Lowpass){
      const double c=std::cos(w),alpha=std::sin(w)/(2*s.q),a0=1+alpha;
      a1=-2*c/a0;a2=(1-alpha)/a0;
      if(s.kind==DspKind::Highpass){b0=(1+c)/(2*a0);b1=-2*b0;}
      else {b0=(1-c)/(2*a0);b1=2*b0;}
      b2=b0;
    }
    std::array<double,2> z1{},z2{};
    for(std::size_t n=0;n<pcm.size();++n){
      const auto ch=n%channels;const double x=pcm[n]/32768.;double y;
      if(s.kind==DspKind::Distortion)y=(1-s.value)*x+s.value*std::tanh(drive*x)/norm;
      else {y=b0*x+z1[ch];z1[ch]=b1*x-a1*y+z2[ch];z2[ch]=b2*x-a2*y;}
      pcm[n]=static_cast<int16_t>(std::lround(std::clamp(y*32768.,-32768.,32767.)));
    }
  }
  return true;
}
} // namespace hulk::eor::audio
