#pragma once
#include "eor_fmod_start_trace.h"
#include <bit>
#include <cmath>
namespace hulk::eor::audio {
// I3DL2-compatible subset of the legacy 124-byte FMOD reverb properties.
// Room gains are hundredths of dB, delays and decay are seconds.
struct OriginalFmodReverbRoom {
  int32_t room=-1000,room_hf=-100,reflections=-2602,reverb=200;
  float rolloff=0.f,decay=1.49f,decay_hf=.83f,reflections_delay=.007f,
      reverb_delay=.011f,diffusion=100.f,density=100.f,hf_reference=5000.f;
  bool operator==(const OriginalFmodReverbRoom&) const noexcept = default;
};
inline bool ValidOriginalFmodReverbRoom(const OriginalFmodReverbRoom& p) noexcept {
  const auto range=[](float x,float lo,float hi){return std::isfinite(x)&&x>=lo&&x<=hi;};
  return p.room>=-10000&&p.room<=0&&p.room_hf>=-10000&&p.room_hf<=0&&
      p.reflections>=-10000&&p.reflections<=1000&&p.reverb>=-10000&&p.reverb<=2000&&
      range(p.rolloff,0,10)&&range(p.decay,.1f,20)&&range(p.decay_hf,.1f,2)&&
      range(p.reflections_delay,0,.3f)&&range(p.reverb_delay,0,.1f)&&
      range(p.diffusion,0,100)&&range(p.density,0,100)&&range(p.hf_reference,1000,20000);
}
template<class Read>
std::optional<OriginalFmodReverbRoom> ReadOriginalFmodReverbRoom(uint32_t owner,Read&& read) noexcept {
  const auto word=[&](uint32_t offset){return FmodTraceWord(owner,offset,read);};
  // USA 8278C540 clamps and copies FMOD_REVERB_PROPERTIES into owner+24.
  const auto room=word(40),hf=word(44),refl=word(64),rev=word(84),roll=word(132),
      decay=word(52),ratio=word(56),early=word(68),late=word(88),diff=word(136),dens=word(140),ref=word(124);
  if(!room||!hf||!refl||!rev||!roll||!decay||!ratio||!early||!late||!diff||!dens||!ref)return {};
  OriginalFmodReverbRoom out{std::bit_cast<int32_t>(*room),std::bit_cast<int32_t>(*hf),
      std::bit_cast<int32_t>(*refl),std::bit_cast<int32_t>(*rev),std::bit_cast<float>(*roll),
      std::bit_cast<float>(*decay),std::bit_cast<float>(*ratio),std::bit_cast<float>(*early),
      std::bit_cast<float>(*late),std::bit_cast<float>(*diff),std::bit_cast<float>(*dens),std::bit_cast<float>(*ref)};
  return ValidOriginalFmodReverbRoom(out)?std::optional<OriginalFmodReverbRoom>(out):std::nullopt;
}
} // namespace hulk::eor::audio
