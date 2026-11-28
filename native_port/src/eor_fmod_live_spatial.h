#pragma once
#include "eor_fmod_start_trace.h"
#include "eor_spatial_audio.h"
#include <bit>
namespace hulk::eor::audio {
// Raw FMOD coordinates, not a claim of matching X3DAudio orientation or rolloff.
struct LiveFmodSpatial {
  SpatialPose listener;
  SpatialVector listener_velocity, position, velocity;
  uint32_t core_flags=0;
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
  if(!lp||!lv||!lf||!lu||!pos||!vel)return {};
  SpatialPose listener{*lp,*lf,*lu};
  if(!ValidSpatialPose(listener)||word(channel,112)!=handle||word(channel,48)!=sound||word(event_system,16)!=core)return {};
  return LiveFmodSpatial{listener,*lv,*pos,*vel,*flags};
}
} // namespace hulk::eor::audio
