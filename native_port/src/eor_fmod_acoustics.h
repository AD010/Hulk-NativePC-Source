#pragma once
#include "eor_fmod_start_trace.h"
#include "eor_spatial_audio.h"
#include <bit>
#include <algorithm>
#include <optional>
namespace hulk::eor::audio {
inline bool FmodUnit(float x) noexcept {return std::isfinite(x)&&x>=0.f&&x<=1.f;}
struct OriginalFmodAcoustics {
  float direct=0.f,reflected=0.f,target_direct=0.f,target_reflected=0.f;
  float direct_gain=1.f,auxiliary_gain=1.f;
};
inline bool ValidOriginalFmodAcoustics(const OriginalFmodAcoustics& a) noexcept {
  return FmodUnit(a.direct)&&FmodUnit(a.reflected)&&FmodUnit(a.target_direct)&&
      FmodUnit(a.target_reflected)&&FmodUnit(a.direct_gain)&&FmodUnit(a.auxiliary_gain)&&
      std::abs(a.direct_gain-(1.f-a.direct))<=1.e-6f;
}
// USA 827955D0 clamps both inputs and stores 1-direct at channel+276.
// 82795970..82795A94 moves current values toward their targets at .002/ms.
inline std::optional<float> AdvanceOriginalFmodOcclusion(float current,float target,uint32_t elapsed,bool immediate=false) noexcept {
  if(!FmodUnit(current)||!FmodUnit(target)||elapsed>0x7fffffffu)return {};
  if(immediate)return target;
  const float step=.002f;
  if(current<target)return (std::min)(target,std::fma(float(elapsed),step,current));
  if(current>target)return (std::max)(target,std::fma(-float(elapsed),step,current));
  return current;
}
// USA 8278CE70: reflected gain is 10^(-3*t), secondary linear weight 1-t.
// Outside the outer radius both are zero; the non-positional case is 1/0.
struct OriginalFmodZoneWeight {float reflected=0.f,linear=0.f;};
inline std::optional<OriginalFmodZoneWeight> EvaluateOriginalFmodZone(float distance,float minimum,float maximum,bool positional) noexcept {
  if(!positional)return OriginalFmodZoneWeight{1.f,0.f};
  if(!std::isfinite(distance)||!std::isfinite(minimum)||!std::isfinite(maximum)||distance<0.f||minimum<0.f||maximum<minimum)return {};
  if(distance<=minimum)return OriginalFmodZoneWeight{1.f,1.f};
  if(distance>=maximum)return OriginalFmodZoneWeight{};
  const float width=maximum-minimum;
  if(width<=0.f)return OriginalFmodZoneWeight{1.f,1.f};
  const float t=(distance-minimum)/width;
  return OriginalFmodZoneWeight{static_cast<float>(std::pow(10.0,double(t*(-3.f)))),1.f-t};
}
// 82790940 applies geometry's reflected occlusion to the zone gain.
inline std::optional<float> OriginalFmodZoneSend(float zone_gain,float reflected_occlusion) noexcept {
  if(!FmodUnit(zone_gain)||!FmodUnit(reflected_occlusion))return {};
  return (1.f-reflected_occlusion)*zone_gain;
}
template<class Read>
std::optional<float> ReadFmodAcousticUnit(uint32_t address,uint32_t offset,Read&& read) noexcept {
  const auto v=FmodTraceWord(address,offset,read);if(!v)return {};
  const float f=std::bit_cast<float>(*v);return FmodUnit(f)?std::optional<float>(f):std::nullopt;
}
template<class Read>
std::optional<OriginalFmodAcoustics> ReadOriginalFmodAcoustics(uint32_t channel,Read&& read) noexcept {
  const auto a=ReadFmodAcousticUnit(channel,356,read),b=ReadFmodAcousticUnit(channel,360,read),
      c=ReadFmodAcousticUnit(channel,364,read),d=ReadFmodAcousticUnit(channel,368,read),
      gain=ReadFmodAcousticUnit(channel,276,read),aux=ReadFmodAcousticUnit(channel,280,read);
  if(!a||!b||!c||!d||!gain||!aux)return {};
  OriginalFmodAcoustics result{*a,*b,*c,*d,*gain,*aux};
  return ValidOriginalFmodAcoustics(result)?std::optional<OriginalFmodAcoustics>(result):std::nullopt;
}
// Original per-channel reverb send arrays (+152, indexed by channel+28).
// These are routing weights, NOT the actual reverberated PCM or DSP parameters.
struct OriginalFmodReverbSend {uint32_t owner=0;float gain=0.f;bool global=false;};
struct OriginalFmodReverbSends {std::array<OriginalFmodReverbSend,64> values{};size_t count=0;};
template<class Read>
std::optional<OriginalFmodReverbSends> ReadOriginalFmodReverbSends(uint32_t core,uint32_t channel,uint32_t handle,Read&& read) noexcept {
  const auto word=[&](uint32_t p,uint32_t o){return FmodTraceWord(p,o,read);};
  if(core>UINT32_MAX-36368u||word(channel,40)!=core||word(channel,112)!=handle)return {};
  const auto index=word(channel,28),count=word(core,332);
  if(!index||!count||*index>=*count||*index!=((handle>>16)&0xfffu))return {};
  OriginalFmodReverbSends out;
  const auto append=[&](uint32_t node,bool global)->bool{
    if(word(node,12)!=core)return false;
    const auto dsp=word(node,16);if(!dsp)return false;
    if(!*dsp)return true;
    const auto array=word(node,152);
    if(!array||!*array||out.count==out.values.size()||uint64_t(*array)+4ull*(*index)>UINT32_MAX-3u)return false;
    const auto gain=ReadFmodAcousticUnit(*array,4u*(*index),read);
    if(!gain)return false;
    out.values[out.count++]={node,*gain,global};return true;
  };
  if(!append(core+36172u,true))return {};
  const uint32_t end=core+36364u;
  auto node=word(core,36364u);std::array<uint32_t,64> seen{};size_t n=0;
  while(node&&*node!=end){
    if(!*node||n==seen.size()||std::find(seen.begin(),seen.begin()+n,*node)!=seen.begin()+n)return {};
    seen[n++]=*node;
    if(!append(*node,false))return {};
    node=word(*node,0);
  }
  if(!node||word(channel,112)!=handle||word(channel,40)!=core)return {};
  return out;
}
} // namespace hulk::eor::audio
