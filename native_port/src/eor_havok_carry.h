#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
namespace hulk::eor::havok {
// USA 8261188C fnmsubs; 826118B8 fadds; 82611904 fsubs.
// Count the actual single-precision carries, including exact equality.
inline std::optional<uint32_t> OriginalFractionalCarries(float remainder,
    float elapsed,float step,uint32_t whole) noexcept {
  if(!std::isfinite(remainder)||remainder<0.f||!std::isfinite(elapsed)||elapsed<0.f||
     !std::isfinite(step)||step<=0.f||whole>16u)return {};
  const float fraction=(std::max)(0.f,std::fma(-step,float(whole),elapsed));
  float accumulator=remainder+fraction;
  if(!std::isfinite(accumulator))return {};
  uint32_t count=0;
  while(accumulator>=step){
    if(count==8u)return {};
    accumulator=(std::max)(0.f,accumulator-step);++count;
  }
  return count;
}
struct OriginalCarryAdmission {float accumulator=0.f;uint32_t extra=0;};
inline std::optional<OriginalCarryAdmission> AdmitOriginalCatchup(float remainder,
    float elapsed,float step,uint32_t whole,uint32_t admitted,uint32_t requested,
    uint32_t max_steps) noexcept {
  const auto baseline=OriginalFractionalCarries(remainder,elapsed,step,whole);
  if(!baseline||admitted>max_steps||*baseline>max_steps-admitted)return {};
  requested=(std::min)(requested,max_steps-admitted-*baseline);
  for(uint32_t extra=requested;;--extra){
    const float accumulator=remainder+float(extra)*step;
    const auto actual=OriginalFractionalCarries(accumulator,elapsed,step,whole);
    if(actual&&*actual==*baseline+extra&&*actual<=max_steps-admitted)
      return OriginalCarryAdmission{accumulator,extra};
    if(!extra)return {};
  }
}
} // namespace hulk::eor::havok
