#pragma once
#include "eor_fmod_start_trace.h"
#include "eor_spatial_audio.h"
#include "eor_fmod_doppler.h"
#include "eor_fmod_acoustics.h"
#include <bit>
namespace hulk::eor::audio {
// Raw FMOD coordinates, not a claim of matching X3DAudio orientation or rolloff.
struct OriginalFmod3dFactors {
  float rolloff=1.f,cone=1.f,doppler=1.f;
  float global_doppler=1.f,distance_factor=1.f,global_rolloff=1.f,channel_doppler=1.f;
};
inline bool ValidOriginalFmod3dFactors(const OriginalFmod3dFactors& f) noexcept {
  return std::isfinite(f.rolloff)&&f.rolloff>=0.f&&f.rolloff<=1.f&&
      std::isfinite(f.cone)&&f.cone>=0.f&&f.cone<=1.f&&
      std::isfinite(f.doppler)&&f.doppler>=1.e-6f&&
      std::isfinite(f.global_doppler)&&f.global_doppler>=0.f&&
      std::isfinite(f.distance_factor)&&f.distance_factor>0.f&&
      std::isfinite(f.global_rolloff)&&f.global_rolloff>=0.f&&
      std::isfinite(f.channel_doppler)&&f.channel_doppler>=0.f;
}
struct LiveFmodSpatial {
  SpatialPose listener;
  SpatialVector listener_velocity, position, velocity;
  uint32_t core_flags=0,mode=0;
  float min_distance=0.f,max_distance=0.f;
  std::optional<OriginalFmod3dFactors> original={};
  std::optional<OriginalFmodAcoustics> acoustics={};
  std::optional<OriginalFmodReverbSends> reverb_sends={};
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
  if(word(channel,112)!=handle||word(channel,40)!=core||word(*core,33724)!=1u)return {};
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
  const auto scalar=[&](uint32_t p,uint32_t o)->std::optional<float>{
    const auto v=word(p,o);if(!v)return {};return std::bit_cast<float>(*v);
  };
  // Original update 82790360/64/68 publishes these three results; the
  // original 3D settings setter/getter uses +33736/+33728/+33732.
  const auto rolloff=scalar(channel,284),cone=scalar(channel,328),doppler=scalar(channel,288),
      global_doppler=scalar(*core,33736),distance_factor=scalar(*core,33728),
      global_rolloff=scalar(*core,33732),channel_doppler=scalar(channel,388);
  if(!rolloff||!cone||!doppler||!global_doppler||!distance_factor||!global_rolloff||!channel_doppler)return {};
  const OriginalFmod3dFactors original{*rolloff,*cone,*doppler,*global_doppler,*distance_factor,*global_rolloff,*channel_doppler};
  if(!ValidOriginalFmod3dFactors(original))return {};
  const auto acoustics=ReadOriginalFmodAcoustics(channel,read);
  if(!acoustics)return {};
  const auto reverb_sends=ReadOriginalFmodReverbSends(*core,channel,handle,read);
  SpatialPose listener{*lp,*lf,*lu};
  if(!ValidSpatialPose(listener)||word(channel,112)!=handle||word(channel,48)!=sound||word(channel,40)!=core||word(*core,33724)!=1u||word(event_system,16)!=core)return {};
  return LiveFmodSpatial{listener,*lv,*pos,*vel,*flags,*mode,minimum,maximum,original,acoustics,reverb_sends};
}
// FMOD Ex mode bits (old ABI). The native shadow uses a unit rolloff
// factor; the original global rolloff, cones and occlusion are not yet
// reproduced. Custom curves are not admitted via this approximation.
inline std::optional<SpatialVoiceState> ResolveLiveFmodSpatial(const LiveFmodSpatial& raw) noexcept {
  constexpr uint32_t head_relative=0x00040000u,world_relative=0x00080000u,
      log_rolloff=0x00100000u,linear_rolloff=0x00200000u,custom_rolloff=0x04000000u;
  if(!(raw.mode&16u)||((raw.mode&custom_rolloff)&&!raw.original)||
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
  if(raw.original){
    if(!ValidOriginalFmod3dFactors(*raw.original))return {};
    out.distance_gain=raw.original->rolloff*raw.original->cone;
    if(raw.acoustics){
      if(!ValidOriginalFmodAcoustics(*raw.acoustics))return {};
      out.distance_gain*=raw.acoustics->auxiliary_gain;
      out.distance_gain*=raw.acoustics->direct_gain;
    }
    out.doppler_factor=raw.original->doppler;
    out.doppler_from_source=true;
    return ValidSpatialState(out)?std::optional<SpatialVoiceState>(out):std::nullopt;
  }
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
  // Experimental unit-scale Doppler; original global/channel factors and
  // world-unit conversion remain unverified. Head-relative has no listener motion.
  const auto factor=NativeDopplerFactor(out.listener.position,out.emitter.position,
      raw.mode&head_relative?SpatialVector{}:convert(raw.listener_velocity),
      convert(raw.velocity),343.5f,1.f);
  if(!factor)return {};
  out.doppler_factor=*factor;
  return ValidSpatialState(out)?std::optional<SpatialVoiceState>(out):std::nullopt;
}
} // namespace hulk::eor::audio
