#pragma once
#include <array>
#include <cmath>
#include <span>
namespace hulk::eor::audio {
// Caller supplies verified world-space transforms in X3DAudio's left-handed
// convention. The game coordinate conversion and FMOD rolloff are not inferred.
struct SpatialVector {float x=0,y=0,z=0;};
struct SpatialPose {SpatialVector position{},front{0,0,1},up{0,1,0};};
struct SpatialVoiceState {
  SpatialPose listener{},emitter{};
  float distance_scale=1.f,channel_radius=0.f;
  // Negative means use the existing X3DAudio distance curve. A nonnegative
  // value supplies an explicit spatial-only gain; 2D remains unchanged.
  float distance_gain=-1.f;
};
inline double SpatialDot(SpatialVector a,SpatialVector b) noexcept {
  return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;
}
inline bool ValidSpatialVector(SpatialVector v) noexcept {
  return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
      std::abs(v.x)<=1.e9f&&std::abs(v.y)<=1.e9f&&std::abs(v.z)<=1.e9f;
}
inline bool ValidSpatialPose(const SpatialPose& p) noexcept {
  return ValidSpatialVector(p.position)&&ValidSpatialVector(p.front)&&ValidSpatialVector(p.up)&&
      std::abs(SpatialDot(p.front,p.front)-1.)<1.e-4&&
      std::abs(SpatialDot(p.up,p.up)-1.)<1.e-4&&std::abs(SpatialDot(p.front,p.up))<1.e-4;
}
inline bool ValidSpatialState(const SpatialVoiceState& s) noexcept {
  return ValidSpatialPose(s.listener)&&ValidSpatialPose(s.emitter)&&
      std::isfinite(s.distance_scale)&&s.distance_scale>=1.e-4f&&s.distance_scale<=1.e9f&&
      std::isfinite(s.channel_radius)&&s.channel_radius>=0.f&&s.channel_radius<=1.e9f&&
      (s.distance_gain==-1.f||(std::isfinite(s.distance_gain)&&s.distance_gain>=0.f&&s.distance_gain<=1.f));
}
// Blend the native 2D routing matrix and X3DAudio matrix. This is a native
// output policy, not a claim of the proprietary FMOD speaker mix law.
inline bool BlendSpatialMatrix(std::span<const float> flat,std::span<const float> spatial,
    float level,std::span<float> result) noexcept {
  if(flat.empty()||flat.size()!=spatial.size()||flat.size()!=result.size()||
      !std::isfinite(level)||level<0.f||level>1.f)return false;
  for(size_t i=0;i<flat.size();++i)
    if(!std::isfinite(flat[i])||!std::isfinite(spatial[i])||std::abs(flat[i])>16.f||std::abs(spatial[i])>16.f)return false;
  for(size_t i=0;i<flat.size();++i)result[i]=std::lerp(flat[i],spatial[i],level);
  return true;
}
} // namespace hulk::eor::audio
