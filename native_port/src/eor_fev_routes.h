#pragma once
#include "eor_fev_event_table.h"
#include <cmath>
namespace hulk::eor::audio {
// USA 8283E380 loader: serialized envelope flags (+8) and controlling
// parameter ordinal (+32). The linked envelope ordinal (+24) indexes
// the layer effect list, as resolved by USA 8282F5F0.
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
// Keep InstanceControl enum for source compatibility; it denotes a linked
// envelope, not an index into the sound instance array.
inline const FevEffectRecord* ResolveFevLinkedEnvelope(const FevLayerRecord& layer,size_t ordinal) noexcept {
  if(ordinal>=layer.effects.size())return nullptr;
  const auto& effect=layer.effects[ordinal];
  if(effect.control>=ordinal||!effect.name.empty()||effect.header[0]!=1||effect.header[1]!=4)return nullptr;
  const auto& target=layer.effects[effect.control];
  if(target.control!=UINT32_MAX||target.header[0]!=0)return nullptr;
  return &target;
}
struct FevGainPitch {
  float gain=1.f,ratio=1.f;
  std::optional<float> pan,pan3d_level;
  std::optional<uint32_t> offset_ms;
  bool operator==(const FevGainPitch&) const noexcept = default;
};
// USA 82830CCC..82830CD4 (pan), 82839540..828395A0 (seek offset).
inline std::optional<uint32_t> OriginalFevOffsetMs(float value,float base_seconds=0.f) noexcept {
  if(!std::isfinite(value)||value<0.f||value>1.f||!std::isfinite(base_seconds)||base_seconds<0.f)return {};
  const float seconds=std::fma(float(std::exp(double(value*5.020596981048584f)))-1.f,0.3986665606498718f,base_seconds);
  const float rounded=std::fma(seconds,1000.f,.5f);
  if(!std::isfinite(rounded)||rounded<0.f||double(rounded)>2147483647.)return {};
  return static_cast<uint32_t>(rounded);
}
inline bool AccumulateFevPosition(FevGainPitch& out,FevTarget target,float value) noexcept {
  if(!std::isfinite(value)||value<0.f||value>1.f)return false;
  if(target==FevTarget::Pan){out.pan=std::fma(1.f-value,2.f,-1.f);return true;}
  if(target==FevTarget::Pan3DLevel){out.pan3d_level=value;return true;}
  if(target==FevTarget::TimeOffset){out.offset_ms=OriginalFevOffsetMs(value);return out.offset_ms.has_value();}
  return false;
}
// Wrap the original millisecond seek against the decoded sample length.
inline uint32_t FevOffsetFrame(uint32_t ms,uint32_t frames,uint32_t rate) noexcept {
  if(!frames||!rate)return 0;
  const uint64_t duration_ms=uint64_t(frames)*1000u/rate;
  if(!duration_ms)return 0;
  return static_cast<uint32_t>((uint64_t(ms)%duration_ms)*rate/1000u);
}
// Host stereo output adapter, not a claim of bit-exact FMOD speaker mixing.
inline std::optional<std::array<float,4>> FevStereoMatrix(float pan,uint16_t channels) noexcept {
  if(!std::isfinite(pan)||pan< -1.f||pan>1.f)return {};
  if(channels==1)return std::array<float,4>{.5f-pan*.5f,.5f+pan*.5f,0.f,0.f};
  if(channels==2)return std::array<float,4>{1.f-(std::max)(pan,0.f),0.f,0.f,1.f+(std::min)(pan,0.f)};
  return {};
}
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
