#pragma once
#include "eor_spatial_audio.h"
#include <algorithm>
#include <optional>
namespace hulk::eor::audio {
// Native shadow policy, not the original FMOD Doppler kernel. Inputs are
// left-handed world units per second; the speed is explicit and configurable.
inline std::optional<float> NativeDopplerFactor(SpatialVector listener,SpatialVector emitter,
    SpatialVector listener_velocity,SpatialVector emitter_velocity,float speed,float scale) noexcept {
  if(!ValidSpatialVector(listener)||!ValidSpatialVector(emitter)||
     !ValidSpatialVector(listener_velocity)||!ValidSpatialVector(emitter_velocity)||
     !std::isfinite(speed)||speed<=0.f||!std::isfinite(scale)||scale<0.f||scale>10.f)return {};
  const double x=double(emitter.x)-listener.x,y=double(emitter.y)-listener.y,z=double(emitter.z)-listener.z;
  const double length=std::sqrt(x*x+y*y+z*z);
  if(length<=1.e-6||scale==0.f)return 1.f;
  const auto radial=[&](SpatialVector v){return std::clamp((x*v.x+y*v.y+z*v.z)*scale/length,-.9*speed,.9*speed);};
  // Positive velocity is along listener-to-source: listener approaches,
  // emitter recedes. Clamp near sonic singularities for host stability.
  const double factor=(speed+radial(listener_velocity))/(speed+radial(emitter_velocity));
  return static_cast<float>(std::clamp(factor,.5,2.));
}
inline std::optional<float> ComposeSpatialPitch(float base,float automation,float doppler,float level,float ceiling) noexcept {
  if(!std::isfinite(base)||!std::isfinite(automation)||!std::isfinite(doppler)||
     !std::isfinite(level)||!std::isfinite(ceiling)||doppler<.5f||doppler>2.f||level<0.f||level>1.f||
     base<=0.f||automation<=0.f||ceiling<.0625f||ceiling>16.f)return {};
  const float dry=base*automation;
  if(!std::isfinite(dry)||dry<.0625f||dry>ceiling)return {};
  return std::clamp(dry*std::lerp(1.f,doppler,level),.0625f,ceiling);
}
} // namespace hulk::eor::audio
