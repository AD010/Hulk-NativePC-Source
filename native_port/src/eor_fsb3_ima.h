#pragma once
#include "eor_fsb3_bank.h"
#include <algorithm>
#include <array>
namespace hulk::eor::audio {
// Xbox/MS IMA blocks: 64 output frames, predictor first, final nibble unused.
inline std::optional<std::vector<int16_t>> DecodeOriginalIma(const Fsb3Sample& s) {
  constexpr uint32_t ima=0x00400000u;
  constexpr uint32_t other=0x00000200u|0x00800000u|0x01000000u|0x02000000u|0x04000000u|0x08000000u;
  if(!(s.mode&ima)||(s.mode&other)||!s.frames||s.frames%64u||
     (s.channels!=1u&&s.channels!=2u)||s.rate<4000u||s.rate>96000u||
     uint64_t(s.frames)*s.channels>(8u<<20))return {};
  const size_t stride=36u*s.channels, needed=size_t(s.frames/64u)*stride;
  if(s.payload.size()<needed||s.payload.size()-needed>=32u)return {};
  for(auto b:s.payload.subspan(needed))if(b!=std::byte{})return {};
  constexpr std::array<int,89> steps{7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
  constexpr std::array<int,8> adjust{-1,-1,-1,-1,2,4,6,8};
  std::vector<int16_t> pcm(size_t(s.frames)*s.channels);
  const auto byte=[&](size_t i){return std::to_integer<uint8_t>(s.payload[i]);};
  for(size_t block=0;block<needed/stride;++block)for(unsigned c=0;c<s.channels;++c){
    const size_t base=block*stride, h=base+4u*c;
    const unsigned raw=AudioU16(s.payload,h);
    int predictor=raw<32768u?int(raw):int(raw)-65536;
    int index=byte(h+2);
    if(index>88||byte(h+3))return {};
    pcm[block*64u*s.channels+c]=static_cast<int16_t>(predictor);
    for(unsigned n=0;n<63u;++n){
      const size_t pos=base+4u*s.channels+4u*c+(n/8u)*4u*s.channels+(n%8u)/2u;
      const unsigned code=(byte(pos)>>(4u*(n&1u)))&15u;
      const int step=steps[index];
      int diff=step>>3;
      if(code&1u)diff+=step>>2;
      if(code&2u)diff+=step>>1;
      if(code&4u)diff+=step;
      predictor=std::clamp(predictor+((code&8u)?-diff:diff),-32768,32767);
      index=std::clamp(index+adjust[code&7u],0,88);
      pcm[(block*64u+n+1u)*s.channels+c]=static_cast<int16_t>(predictor);
    }
  }
  return pcm;
}
} // namespace hulk::eor::audio
