#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include "eor_dynamic_dsp.h"
#include "eor_fev_voice_schedule.h"
#include "eor_spatial_audio.h"
#include "eor_reverb_route.h"
namespace hulk::eor::audio {
enum class VoiceFilter : uint8_t { Bypass, LowPass, HighPass };
struct VoiceOptions {
  uint32_t delay_ms=0,loop_begin=0,loop_length=0,loop_count=0;
  float frequency_ratio=1.f;
  VoiceFilter filter=VoiceFilter::Bypass;
  float filter_frequency=1.f,filter_one_over_q=1.f;
  bool start_paused=false;
  // Reserve the full original pitch range for explicitly live-controlled voices.
  bool reserve_live_pitch=false;
  // One-shot DSP; resolved blocks are borrowed only during PlayPcm16.
  StaticDspChain static_dsp{};
  std::span<const DspParameterBlock> dsp_blocks{};
  uint32_t start_frame=0;
  std::optional<float> stereo_pan;
  std::span<const FevVoiceBlock> fev_voice_blocks{};
  std::optional<SpatialVoiceState> spatial;
  std::optional<float> spatial_level;
  // Opt-in single-snapshot FEV streaming DSP, not a pre-rendered buffer.
  bool live_fev_dsp=false;
  // Explicit opt-in to a separate, native approximate wet path.
  std::optional<float> reverb_send;
  NativeRoomRoutes room_routes;
};
inline bool ValidVoiceFilter(VoiceFilter filter,float frequency,float one_over_q) noexcept {
  return (filter==VoiceFilter::Bypass||filter==VoiceFilter::LowPass||filter==VoiceFilter::HighPass)&&
      std::isfinite(frequency)&&frequency>=0.f&&frequency<=1.f&&
      std::isfinite(one_over_q)&&one_over_q>0.f&&one_over_q<=1.5f;
}
inline bool ValidVoiceRatio(float ratio) noexcept {
  return ValidFevVoiceRatio(ratio);
}
inline bool ValidVoiceOptions(const VoiceOptions& o,uint32_t frames) noexcept {
  if(!frames||o.delay_ms>600000u||o.loop_count>255u||!ValidVoiceRatio(o.frequency_ratio)||
     !ValidVoiceFilter(o.filter,o.filter_frequency,o.filter_one_over_q))return false;
  if(o.static_dsp.count>o.static_dsp.stages.size() ||
     ((o.static_dsp.count||!o.dsp_blocks.empty())&&o.loop_count) ||
     (o.static_dsp.count&&!o.dsp_blocks.empty()))return false;
  const bool moving=HasMovingFevOffset(o.fev_voice_blocks);
  if(o.live_fev_dsp&&(o.static_dsp.count||o.loop_count||moving||o.dsp_blocks.size()!=1||
      o.dsp_blocks.front().first_frame!=0||o.fev_voice_blocks.size()!=1))return false;
  // Pre-rendered PCM DSP follows the source position, not the independent
  // event clock. Do not replay its stale history after an automatic seek.
  if(moving&&(o.static_dsp.count||std::any_of(o.dsp_blocks.begin(),o.dsp_blocks.end(),
      [](const auto& b){return b.chain.count!=0;})))return false;
  if((!o.fev_voice_blocks.empty()&&o.loop_count)||!ValidFevVoiceSchedule(o.fev_voice_blocks,frames,o.frequency_ratio,o.spatial.has_value(),true))return false;
  if(o.spatial&&!ValidSpatialState(*o.spatial))return false;
  if(o.reverb_send&&(!std::isfinite(*o.reverb_send)||*o.reverb_send<0.f||*o.reverb_send>1.f||o.room_routes.count))return false;
  if(!o.room_routes.Valid(ValidOriginalFmodReverbRoom))return false;
  if(o.spatial_level&&(!o.spatial||!std::isfinite(*o.spatial_level)||*o.spatial_level<0.f||*o.spatial_level>1.f))return false;
  if(o.start_frame>=frames||(o.start_frame&&o.loop_count)||
     (o.stereo_pan&&(!std::isfinite(*o.stereo_pan)||*o.stereo_pan< -1.f||*o.stereo_pan>1.f)))return false;
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
