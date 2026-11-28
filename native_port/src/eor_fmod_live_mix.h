#pragma once
#include "eor_fmod_start_trace.h"
#include <bit>
#include <cmath>
namespace hulk::eor::audio {
struct LiveFmodAudibility {
  float channel=0.f,group=0.f,combined=0.f;
  bool spatial=false;
};
// Exact operation order of the USA scalar audibility getter 827925E0.
// This is not the speaker matrix, DSP output or final mixed PCM.
template<class Read>
std::optional<LiveFmodAudibility> CaptureLiveFmodAudibility(uint32_t event_system,uint32_t handle,Read&& read) noexcept {
  const auto word=[&](uint32_t p,uint32_t o){return FmodTraceWord(p,o,read);};
  const auto scalar=[&](uint32_t p,uint32_t o)->std::optional<float>{
    const auto w=word(p,o);if(!w)return {};
    const float f=std::bit_cast<float>(*w);
    return std::isfinite(f)&&f>=0.f?std::optional<float>(f):std::nullopt;
  };
  if(!CaptureFmodUpdateStamp(event_system,read)||!(handle&0xffffu))return {};
  const auto core=word(event_system,16);
  if(!core||!*core||word(*core,34140)!=(handle>>28))return {};
  const auto count=word(*core,332),array=word(*core,336);
  const uint32_t index=(handle>>16)&0xfffu;
  if(!count||!array||!*array||index>=*count)return {};
  const uint64_t address=uint64_t(*array)+416ull*index;
  if(address>UINT32_MAX-415u)return {};
  const auto channel=static_cast<uint32_t>(address);
  if(word(channel,112)!=handle||word(channel,40)!=core)return {};
  const auto sound=word(channel,48),group=word(channel,140);
  if(!sound||!*sound||!group||!*group)return {};
  const auto mode=word(*sound,100);
  const auto base=scalar(channel,164),group_base=scalar(*group,60);
  if(!mode||!base||!group_base)return {};
  const bool spatial=(*mode&16u)!=0;
  float value=*group_base;
  if(spatial){
    const auto g=scalar(*group,72),a=scalar(channel,328),b=scalar(channel,284),
        c=scalar(channel,280),d=scalar(channel,276);
    if(!g||!a||!b||!c||!d)return {};
    value=*g*value;value=value*(*a);value=value*(*b);value=value*(*c);value=value*(*d);
  }
  value=value*(*base);
  if(!std::isfinite(value)||word(channel,112)!=handle||word(channel,48)!=sound||
     word(channel,140)!=group||word(channel,40)!=core||word(event_system,16)!=core)return {};
  return LiveFmodAudibility{*base,*group_base,value,spatial};
}
} // namespace hulk::eor::audio
