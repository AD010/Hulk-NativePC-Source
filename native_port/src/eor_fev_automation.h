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
// Interpolation is not inferred. Constants and exact stored knots do not
// require knowing whether the neighboring segment is flat, logarithmic, etc.
inline std::optional<float> SampleFevCurveExact(const FevEffectRecord& effect,float x) noexcept {
  if(!std::isfinite(x)||x<0||x>1||!ValidFevCurve(effect))return {};
  const auto first=ReadFevCurvePoint(effect.points.front());
  bool constant=true;
  for(const auto& raw:effect.points){
    const auto p=ReadFevCurvePoint(raw);
    if(x==p.position)return p.value;
    constant &= p.value==first.value;
  }
  return constant?std::optional<float>(first.value):std::nullopt;
}
// These associations are supplied by a verified event owner, NOT guessed
// from legacy envelope flags. The caller explicitly chooses a physical
// linear mapping; only constant/exact stored coordinates are evaluated here.
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
      const auto value=SampleFevCurveExact(effects[binding.effect],*x);
      if(!value)return {};
      chain.stages[i]={ClassifyFevDsp(effects[binding.effect].name),
          std::lerp(binding.physical_at_zero,binding.physical_at_one,*value),binding.resonance};
    }
    if(!ValidStaticDsp(chain,sample_rate))return {};
    blocks.push_back({snapshot.first_frame,chain});
  }
  return blocks;
}
} // namespace hulk::eor::audio
