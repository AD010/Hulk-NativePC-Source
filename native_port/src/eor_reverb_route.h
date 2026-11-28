#pragma once
#include "eor_fmod_acoustics.h"
namespace hulk::eor::audio {
// Native fallback: collapse observed zone sends into one wet bus. This is
// not equivalent to multiple independent FMOD reverberation processors.
inline std::optional<float> ResolveNativeReverbSend(const OriginalFmodReverbSends& sends) noexcept {
  if(sends.count>sends.values.size())return {};
  double sum=0.;
  for(size_t i=0;i<sends.count;++i){
    const auto& send=sends.values[i];
    if(!send.owner||!FmodUnit(send.gain))return {};
    for(size_t j=0;j<i;++j)if(sends.values[j].owner==send.owner)return {};
    sum+=send.gain;
  }
  return static_cast<float>((std::min)(sum,1.));
}
inline std::optional<std::array<float,2>> NativeWetMatrix(uint16_t channels,float send) noexcept {
  if(!FmodUnit(send)||(channels!=1&&channels!=2))return {};
  return channels==1?std::array<float,2>{send,0.f}:std::array<float,2>{send*.5f,send*.5f};
}
} // namespace hulk::eor::audio
