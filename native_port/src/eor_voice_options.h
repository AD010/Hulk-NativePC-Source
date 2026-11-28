#pragma once
#include <cmath>
#include <cstdint>
#include "eor_static_dsp.h"
namespace hulk::eor::audio {
enum class VoiceFilter : uint8_t { Bypass, LowPass, HighPass };
struct VoiceOptions {
  uint32_t delay_ms=0,loop_begin=0,loop_length=0,loop_count=0;
  float frequency_ratio=1.f;
  VoiceFilter filter=VoiceFilter::Bypass;
  float filter_frequency=1.f,filter_one_over_q=1.f;
  bool start_paused=false;
  // Static one-shot DSP only; dynamic FMOD curves need their own scheduler.
  StaticDspChain static_dsp{};
};
inline bool ValidVoiceFilter(VoiceFilter filter,float frequency,float one_over_q) noexcept {
  return (filter==VoiceFilter::Bypass||filter==VoiceFilter::LowPass||filter==VoiceFilter::HighPass)&&
      std::isfinite(frequency)&&frequency>=0.f&&frequency<=1.f&&
      std::isfinite(one_over_q)&&one_over_q>0.f&&one_over_q<=1.5f;
}
inline bool ValidVoiceRatio(float ratio) noexcept {
  return std::isfinite(ratio)&&ratio>=0.25f&&ratio<=4.f;
}
inline bool ValidVoiceOptions(const VoiceOptions& o,uint32_t frames) noexcept {
  if(!frames||o.delay_ms>600000u||o.loop_count>255u||!ValidVoiceRatio(o.frequency_ratio)||
     !ValidVoiceFilter(o.filter,o.filter_frequency,o.filter_one_over_q))return false;
  if(o.static_dsp.count>o.static_dsp.stages.size() || (o.static_dsp.count && o.loop_count))return false;
  if(!o.loop_count)return !o.loop_begin&&!o.loop_length;
  if(!o.loop_length)return !o.loop_begin;
  return o.loop_begin<frames&&o.loop_length<=frames-o.loop_begin;
}
struct PendingVoiceClock {
  uint32_t remaining_ms=0;
  bool pending=false,paused=false;
  // Monotonic elapsed game time supplied by the verified event owner.
  bool Advance(uint32_t elapsed_ms) noexcept {
    if(!pending||paused)return false;
    if(elapsed_ms<remaining_ms){remaining_ms-=elapsed_ms;return false;}
    remaining_ms=0;return true;
  }
};
} // namespace hulk::eor::audio
