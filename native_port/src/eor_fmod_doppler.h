#pragma once
#include "eor_spatial_audio.h"
#include <algorithm>
#include <cstdint>
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
// Scalar single-listener branch at USA 82790214..8279033C. The original
// predicts the previous distance from current position and velocity, rather
// than applying the textbook listener/source frequency quotient.
inline std::optional<float> OriginalUsaDopplerSingle(SpatialVector listener,SpatialVector emitter,
    SpatialVector listener_velocity,SpatialVector emitter_velocity,bool head_relative,
    uint32_t elapsed_ms,float global_scale,float distance_factor,float channel_scale) noexcept {
  if(!ValidSpatialVector(listener)||!ValidSpatialVector(emitter)||!ValidSpatialVector(listener_velocity)||
     !ValidSpatialVector(emitter_velocity)||!std::isfinite(global_scale)||global_scale<0.f||
     !std::isfinite(distance_factor)||distance_factor<=0.f||!std::isfinite(channel_scale)||channel_scale<0.f)return {};
  const float scale=channel_scale*global_scale,speed=distance_factor*340.f;
  if(!std::isfinite(scale)||!std::isfinite(speed)||speed<=0.f)return {};
  if(scale==0.f)return 1.f;
  const float dt=elapsed_ms>=1&&elapsed_ms<=1000?float(elapsed_ms)*.001f:1.f;
  const auto length=[](float x,float y,float z){return std::sqrt(std::fma(x,x,std::fma(z,z,y*y)));};
  const auto previous=[&](float pos,float velocity){return pos-velocity*dt;};
  const float current=length(emitter.x-(head_relative?0.f:listener.x),
      emitter.y-(head_relative?0.f:listener.y),emitter.z-(head_relative?0.f:listener.z));
  const float past=length(previous(emitter.x,emitter_velocity.x)-(head_relative?0.f:previous(listener.x,listener_velocity.x)),
      previous(emitter.y,emitter_velocity.y)-(head_relative?0.f:previous(listener.y,listener_velocity.y)),
      previous(emitter.z,emitter_velocity.z)-(head_relative?0.f:previous(listener.z,listener_velocity.z)));
  const float factor=std::fma(-(current-past)/dt,scale,speed)/speed;
  if(!std::isfinite(factor))return {};
  return (std::max)(factor,1.e-6f);
}
inline std::optional<float> ComposeSpatialPitch(float base,float automation,float doppler,float level,float ceiling) noexcept {
  if(!std::isfinite(base)||!std::isfinite(automation)||!std::isfinite(doppler)||
     !std::isfinite(level)||!std::isfinite(ceiling)||doppler<1.e-6f||level<0.f||level>1.f||
     base<=0.f||automation<=0.f||ceiling<.0625f||ceiling>16.f)return {};
  const float dry=base*automation;
  if(!std::isfinite(dry)||dry<.0625f||dry>ceiling)return {};
  return static_cast<float>(std::clamp(double(dry)*std::lerp(1.0,double(doppler),double(level)),.0625,double(ceiling)));
}
} // namespace hulk::eor::audio
