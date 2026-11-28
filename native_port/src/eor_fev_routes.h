#pragma once
#include "eor_fev_event_table.h"
#include <cmath>
namespace hulk::eor::audio {
// USA 8283E380 loader: serialized envelope flags (+8) and controlling
// parameter ordinal (+32). The instance selector (+24) is NOT a verified
// index into the serialized layer sound list; retain it without guessing.
enum class FevTarget : uint8_t { NamedDsp, InstanceControl, Volume, Pitch, Pan, TimeOffset, Pan3DLevel };
struct FevCurveRoute {FevTarget target;uint32_t parameter,instance,ordinal;};
inline std::optional<FevCurveRoute> ReadFevCurveRoute(const FevEventEntry& event,const FevEffectRecord& fx) noexcept {
  if(fx.tail[0]>=event.parameters.size()||fx.tail[1]>1)return {};
  FevTarget target;
  if(!fx.name.empty()){
    if(fx.control!=UINT32_MAX||fx.header[0]!=0||fx.header[1]!=4)return {};
    target=FevTarget::NamedDsp;
  }else if(fx.control!=UINT32_MAX){
    if(fx.header[0]!=1||fx.header[1]!=4)return {};
    target=FevTarget::InstanceControl;
  }else{
    if(fx.header[0]!=0)return {};
    switch(fx.header[1]){
      case 0x0c:target=FevTarget::Volume;break;
      case 0x14:target=FevTarget::Pitch;break;
      case 0x24:target=FevTarget::Pan;break;
      case 0x44:target=FevTarget::TimeOffset;break;
      case 0x404:target=FevTarget::Pan3DLevel;break;
      default:return {};
    }
  }
  return FevCurveRoute{target,fx.tail[0],fx.control,fx.header[0]};
}
struct FevGainPitch {float gain=1.f,ratio=1.f;};
// USA 8282F998..8282FA00 multiplies volume and exp2(8*v-4) pitch
// contributions in layer order. VMX vexptefp is an estimate; exp2 on x64
// is not bit-exact. Do not conflate a normalized pitch curve with Hz.
inline bool AccumulateFevGainPitch(FevGainPitch& out,FevTarget target,float value) noexcept {
  if(!std::isfinite(value)||value<0.f||value>1.f)return false;
  auto next=out;
  if(target==FevTarget::Volume)next.gain*=value;
  else if(target==FevTarget::Pitch)next.ratio*=std::exp2(std::fma(8.f,value,-4.f));
  else return false;
  if(!std::isfinite(next.gain)||!std::isfinite(next.ratio)||next.ratio<=0.f)return false;
  out=next;return true;
}
} // namespace hulk::eor::audio
