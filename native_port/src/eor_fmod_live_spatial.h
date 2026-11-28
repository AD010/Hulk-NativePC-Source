#pragma once
#include "eor_fmod_start_trace.h"
#include "eor_spatial_audio.h"
#include <bit>
namespace hulk::eor::audio {
// Raw FMOD coordinates, not a claim of matching X3DAudio orientation or rolloff.
struct LiveFmodSpatial {
  SpatialPose listener;
  SpatialVector listener_velocity, position, velocity;
  uint32_t core_flags=0,mode=0;
  float min_distance=0.f,max_distance=0.f;
};
template<class Read>
std::optional<SpatialVector> ReadFmodSpatialVector(uint32_t p,uint32_t offset,Read&& read) noexcept {
  if(uint64_t(offset)+8u>UINT32_MAX)return {};
  auto x=FmodTraceWord(p,offset,read),y=FmodTraceWord(p,offset+4u,read),z=FmodTraceWord(p,offset+8u,read);
  if(!x||!y||!z)return {};
  SpatialVector v{std::bit_cast<float>(*x),std::bit_cast<float>(*y),std::bit_cast<float>(*z)};
  return ValidSpatialVector(v)?std::optional<SpatialVector>(v):std::nullopt;
}
template<class Read>
std::optional<LiveFmodSpatial> CaptureLiveFmodSpatial(uint32_t event_system,uint32_t handle,Read&& read) noexcept {
  const auto word=[&](uint32_t p,uint32_t o){return FmodTraceWord(p,o,read);};
  // EventSystem forwards its +16 pointer unchanged after core list validation.
  if(!CaptureFmodUpdateStamp(event_system,read)||!handle||word(kUsaEventSystemVtable,120)!=0x82844368u)return {};
  const auto core=word(event_system,16);
  if(!core||!*core||word(*core,34140)!=(handle>>28))return {};
  const auto flags=word(*core,20),count=word(*core,332),array=word(*core,336);
  const uint32_t index=(handle>>16)&0xfffu;
  if(!flags||!count||!array||!*array||index>=*count||!(handle&0xffffu))return {};
  const uint64_t address=uint64_t(*array)+416ull*index;
  if(address>UINT32_MAX-415u)return {};
  const auto channel=static_cast<uint32_t>(address);
  // Require exact generation; the guest API also accepts generation zero,
  // but it would be unsafe for a persistent native shadow identity.
  if(word(channel,112)!=handle)return {};
  const auto sound=word(channel,48);
  if(!sound||!*sound)return {};
  const auto mode=word(*sound,100);
  if(!mode||!(*mode&16u))return {};
  const auto lp=ReadFmodSpatialVector(*core,33288,read),lv=ReadFmodSpatialVector(*core,33216,read),
      lf=ReadFmodSpatialVector(*core,33252,read),lu=ReadFmodSpatialVector(*core,33228,read),
      pos=ReadFmodSpatialVector(channel,292,read),vel=ReadFmodSpatialVector(channel,304,read);
  const auto lo=word(channel,320),hi=word(channel,324);
  if(!lp||!lv||!lf||!lu||!pos||!vel||!lo||!hi)return {};
  const float minimum=std::bit_cast<float>(*lo),maximum=std::bit_cast<float>(*hi);
  if(!std::isfinite(minimum)||!std::isfinite(maximum)||minimum<0.f||maximum<minimum)return {};
  SpatialPose listener{*lp,*lf,*lu};
  if(!ValidSpatialPose(listener)||word(channel,112)!=handle||word(channel,48)!=sound||word(event_system,16)!=core)return {};
  return LiveFmodSpatial{listener,*lv,*pos,*vel,*flags,*mode,minimum,maximum};
}
// FMOD Ex mode bits (old ABI). The native shadow uses a unit rolloff
// factor; the original global rolloff, cones and occlusion are not yet
// reproduced. Custom curves are not admitted via this approximation.
inline std::optional<SpatialVoiceState> ResolveLiveFmodSpatial(const LiveFmodSpatial& raw) noexcept {
  constexpr uint32_t head_relative=0x00040000u,world_relative=0x00080000u,
      log_rolloff=0x00100000u,linear_rolloff=0x00200000u,custom_rolloff=0x04000000u;
  if(!(raw.mode&16u)||(raw.mode&custom_rolloff)||
     (raw.mode&(head_relative|world_relative))==(head_relative|world_relative)||
     (raw.mode&(log_rolloff|linear_rolloff))==(log_rolloff|linear_rolloff)||
     !ValidSpatialPose(raw.listener)||!ValidSpatialVector(raw.position)||
     !std::isfinite(raw.min_distance)||!std::isfinite(raw.max_distance)||
     raw.min_distance<1.e-4f||raw.max_distance<raw.min_distance||raw.max_distance>1.e9f)return {};
  // FMOD Ex INIT_3D_RIGHTHANDED = 2; X3DAudio is left handed.
  const auto convert=[&](SpatialVector v){if(raw.core_flags&2u)v.z=-v.z;return v;};
  SpatialVoiceState out;
  if(!(raw.mode&head_relative)){
    out.listener={convert(raw.listener.position),convert(raw.listener.front),convert(raw.listener.up)};
  }
  out.emitter.position=convert(raw.position);
  out.distance_scale=raw.min_distance;
  const double x=double(out.emitter.position.x)-out.listener.position.x,
      y=double(out.emitter.position.y)-out.listener.position.y,
      z=double(out.emitter.position.z)-out.listener.position.z;
  const double distance=std::sqrt(x*x+y*y+z*z);
  const double clamped=distance<raw.min_distance?raw.min_distance:
      (distance>raw.max_distance?raw.max_distance:distance);
  if(raw.mode&linear_rolloff){
    if(raw.max_distance==raw.min_distance)return {};
    out.distance_gain=static_cast<float>((raw.max_distance-clamped)/(raw.max_distance-raw.min_distance));
  }else out.distance_gain=static_cast<float>(raw.min_distance/clamped);
  return ValidSpatialState(out)?std::optional<SpatialVoiceState>(out):std::nullopt;
}
} // namespace hulk::eor::audio
