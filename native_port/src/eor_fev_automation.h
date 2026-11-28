#pragma once
#include "eor_fev_event_table.h"
#include "eor_dynamic_dsp.h"
#include <bit>
#include <cmath>
namespace hulk::eor::audio {
struct FevParameterDomain {
  float minimum=0,maximum=1;
  bool Valid() const noexcept {
    return std::isfinite(minimum)&&std::isfinite(maximum)&&maximum>minimum&&std::isfinite(maximum-minimum);
  }
  // USA 8283B1F0 / 8283BDE0: single-precision subtract/divide then clamp.
  std::optional<float> Normalize(float physical) const noexcept {
    if(!Valid()||!std::isfinite(physical))return {};
    return std::clamp((physical-minimum)/(maximum-minimum),0.f,1.f);
  }
  // USA 8283B4B8: single-precision subtract and fused multiply-add.
  std::optional<float> Physical(float normalized) const noexcept {
    if(!Valid()||!std::isfinite(normalized)||normalized<0||normalized>1)return {};
    return std::fma(maximum-minimum,normalized,minimum);
  }
};
inline std::optional<FevParameterDomain> ReadFevParameterDomain(const FevParameterRecord& p) noexcept {
  if(p.fields.size()!=24)return {};
  FevParameterDomain domain{std::bit_cast<float>(AudioU32(p.fields,4)),
                            std::bit_cast<float>(AudioU32(p.fields,8))};
  if(!domain.Valid())return {};
  return domain;
}
struct FevCurvePoint {float position,value;uint32_t shape;};
inline FevCurvePoint ReadFevCurvePoint(const std::array<uint32_t,3>& point) noexcept {
  return {std::bit_cast<float>(point[0]),std::bit_cast<float>(point[1]),point[2]};
}
inline bool ValidFevCurve(const FevEffectRecord& effect) noexcept {
  if(effect.points.empty()||effect.points.size()>4096)return false;
  float previous=-1.f;
  for(const auto& raw:effect.points){
    const auto p=ReadFevCurvePoint(raw);
    if(!std::isfinite(p.position)||!std::isfinite(p.value)||p.position<=previous||
       p.position<0||p.position>1||p.value<0||p.value>1||
       (p.shape!=1&&p.shape!=2&&p.shape!=4&&p.shape!=8))return false;
    previous=p.position;
  }
  return true;
}
// USA 0x8282EBB0: the destination knot selects the segment shape.
// The cubic and linear paths use the original single-precision FMA order;
// the host log/pow implementation may differ in the last bits from PPC.
inline float InterpolateUsaFevSegment(FevCurvePoint a,FevCurvePoint b,float t) noexcept {
  switch(b.shape){
    case 1: {
      // 8282DF58: cubic Bezier with y controls a,a,b,b (de Casteljau).
      const float middle=std::fma(b.value-a.value,t,a.value);
      const float left=std::fma(middle-a.value,t,a.value);
      const float right=std::fma(b.value-middle,t,middle);
      return std::fma(right-left,t,left);
    }
    case 2:return std::fma(t,b.value-a.value,a.value);
    case 4: {
      const bool ascending=b.value>a.value;
      const float low=ascending?a.value:b.value;
      const float delta=ascending?b.value-a.value:a.value-b.value;
      if(!ascending)t=1.f-t;
      const float exponent=(float(std::log(1025.0))/float(std::log(2.0)))*t;
      const float power=float(std::pow(2.0,double(exponent)));
      return std::fma((power-1.f)*delta,0.0009765625f,low);
    }
    case 8: {
      const bool ascending=b.value>a.value;
      const float low=ascending?a.value:b.value;
      const float delta=ascending?b.value-a.value:a.value-b.value;
      if(ascending)t=1.f-t;
      const float centered=t-.5f;
      const float cubic=(centered*centered)*centered;
      return std::fma(std::fma(-4.f,cubic,.5f),delta,low);
    }
    default:return 0.f; // unreachable after ValidFevCurve
  }
}
inline std::optional<float> SampleFevCurve(const FevEffectRecord& effect,float x) noexcept {
  if(!std::isfinite(x)||x<0||x>1||!ValidFevCurve(effect))return {};
  auto previous=ReadFevCurvePoint(effect.points.front());
  // Every original curve starts at zero; reject unverified extrapolation.
  if(previous.position!=0.f)return {};
  if(x==previous.position||effect.points.size()==1)return previous.value;
  for(size_t i=1;i<effect.points.size();++i){
    const auto next=ReadFevCurvePoint(effect.points[i]);
    if(x==next.position)return next.value;
    if(x<next.position){
      const float t=(x-previous.position)/(next.position-previous.position);
      const float value=InterpolateUsaFevSegment(previous,next,t);
      if(!std::isfinite(value))return {};
      // PPC math-library rounding may cause tiny overshoot near a knot.
      return std::clamp(value,(std::min)(previous.value,next.value),(std::max)(previous.value,next.value));
    }
    previous=next;
  }
  return previous.value;
}
// USA FEV loader 8283EA90..8283EAB0 reads the second tail word into
// runtime envelope +48 as a boolean. 8282FE04 / 8282FEE4 map normalized
// envelope output to the DSP parameter's physical min/max. Named filters
// use the frequency branch for parameter 0; distortion uses linear Level.
inline std::optional<float> MapOriginalNamedDspValue(const FevEffectRecord& effect,
    float normalized) noexcept {
  const auto kind=ClassifyFevDsp(effect.name);
  const auto range=OriginalUsaDspRange(kind,effect.header[0]);
  if(!range||effect.header[0]!=0||effect.tail[1]>1||
     !std::isfinite(normalized)||normalized<0.f||normalized>1.f)return {};
  const float low=range->minimum,high=range->maximum;
  if(normalized==0.f)return low;
  if(normalized==1.f)return high;
  if(!effect.tail[1]||kind==DspKind::Distortion)
    return std::fma(high-low,normalized,low);
  if(low<=0.f||high<=0.f)return {};
  const float a=float(std::log(double(low))),b=float(std::log(double(high)));
  const float value=float(std::exp(double(std::fma(b-a,normalized,a))));
  if(!std::isfinite(value))return {};
  return std::clamp(value,low,high);
}
// Physical associations are supplied by the verified event owner. The
// envelope itself now supplies the original DSP range mapping; callers
// still supply the controlling parameter and independent resonance value.
struct FevDspBinding {
  uint32_t effect=0,parameter=0;
  float physical_at_zero=0,physical_at_one=1,resonance=1;
};
struct FevParameterSnapshot {
  uint32_t first_frame=0;
  std::span<const float> physical_values;
};
inline std::optional<std::vector<DspParameterBlock>> BuildFevNamedDspBlocks(
    const FevEventEntry& event,uint32_t layer,std::span<const FevDspBinding> bindings,
    std::span<const FevParameterSnapshot> snapshots,uint32_t sample_rate) {
  if(layer>=event.layer_records.size()||bindings.empty()||bindings.size()>4||
      snapshots.empty()||snapshots.size()>16384||snapshots.front().first_frame!=0)return {};
  const auto& effects=event.layer_records[layer].effects;
  // Do not silently omit an unbound named effect or reorder the chain.
  size_t named=0;
  for(const auto& effect:effects)if(!effect.name.empty()){
    if(named>=bindings.size()||bindings[named].effect!=size_t(&effect-effects.data())||
       ClassifyFevDsp(effect.name)==DspKind::Unknown)return {};
    ++named;
  }
  if(named!=bindings.size())return {};
  std::vector<DspParameterBlock> blocks;blocks.reserve(snapshots.size());
  for(const auto& snapshot:snapshots){
    if((!blocks.empty()&&snapshot.first_frame<=blocks.back().first_frame)||
       snapshot.physical_values.size()!=event.parameters.size())return {};
    StaticDspChain chain;chain.count=static_cast<uint8_t>(bindings.size());
    for(size_t i=0;i<bindings.size();++i){
      const auto& binding=bindings[i];
      if(binding.parameter>=event.parameters.size()||!std::isfinite(binding.physical_at_zero)||
         !std::isfinite(binding.physical_at_one)||!std::isfinite(binding.resonance))return {};
      const auto domain=ReadFevParameterDomain(event.parameters[binding.parameter]);
      if(!domain)return {};
      const auto x=domain->Normalize(snapshot.physical_values[binding.parameter]);
      if(!x)return {};
      const auto value=SampleFevCurve(effects[binding.effect],*x);
      if(!value)return {};
      const auto& effect=effects[binding.effect];
      const auto kind=ClassifyFevDsp(effect.name);
      const auto range=OriginalUsaDspRange(kind,0);
      if(!range||binding.physical_at_zero!=range->minimum||binding.physical_at_one!=range->maximum)return {};
      const auto physical=MapOriginalNamedDspValue(effect,*value);
      if(!physical)return {};
      chain.stages[i]={kind,*physical,binding.resonance};
    }
    if(!ValidStaticDsp(chain,sample_rate))return {};
    blocks.push_back({snapshot.first_frame,chain});
  }
  return blocks;
}
} // namespace hulk::eor::audio
