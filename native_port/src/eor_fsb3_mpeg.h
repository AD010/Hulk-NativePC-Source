#pragma once
#include "eor_fsb3_bank.h"
#include <array>
namespace hulk::eor::audio {
// Original corpus uses MPEG-1/2 Layer II. No resync or guessed padding.
inline std::optional<std::vector<AudioBytes>> OriginalMpegLayer2Frames(const Fsb3Sample& s) {
  if(!(s.mode&0x200u)||(s.mode&0x0fc00000u)||!s.frames||s.frames%1152u||
     (s.channels!=1u&&s.channels!=2u)||s.payload.size()>(64u<<20)||
     s.frames/1152u>s.payload.size()/4u)return {};
  constexpr std::array<uint32_t,15> m1{0,32,48,56,64,80,96,112,128,160,192,224,256,320,384};
  constexpr std::array<uint32_t,15> m2{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
  constexpr std::array<uint32_t,3> rates{44100,48000,32000};
  std::vector<AudioBytes> frames;frames.reserve(s.frames/1152u);
  size_t p=0;
  for(uint32_t i=0;i<s.frames/1152u;++i) {
    if(s.payload.size()-p<4)return {};
    const auto v=AudioU32(s.payload,p,true);
    const auto ver=(v>>19)&3u,layer=(v>>17)&3u,bitrate=(v>>12)&15u,rate=(v>>10)&3u;
    if(v>>21!=2047u||(ver!=2u&&ver!=3u)||layer!=2u||!bitrate||bitrate==15u||rate==3u)return {};
    const auto hz=rates[rate]/(ver==2u?2u:1u);
    const auto channels=((v>>6)&3u)==3u?1u:2u;
    if(hz!=s.rate||channels!=s.channels)return {};
    const auto size=144000u*(ver==3u?m1[bitrate]:m2[bitrate])/hz+((v>>9)&1u);
    if(size<4u||size>s.payload.size()-p)return {};
    frames.push_back(s.payload.subspan(p,size));p+=size;
  }
  if(s.payload.size()-p>=32u)return {};
  for(auto b:s.payload.subspan(p))if(b!=std::byte{})return {};
  return frames;
}
} // namespace hulk::eor::audio
