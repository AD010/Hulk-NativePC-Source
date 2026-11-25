#include "../native_port/src/eor_fsb4_pcm.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>
using namespace hulk::eor::audio;
using Byte=std::byte;
static void Put16(std::vector<Byte>& v,size_t p,uint16_t n) {
  v[p]=Byte(n&255u);v[p+1]=Byte(n>>8);
}
static void Put32(std::vector<Byte>& v,size_t p,uint32_t n) {
  Put16(v,p,uint16_t(n&65535u));Put16(v,p+2,uint16_t(n>>16));
}
int main() {
  // One authentic FSB4-layout non-basic sample, two stereo PCM16 frames.
  std::vector<Byte> bank(48u+80u+8u,Byte{0});
  for(unsigned i=0;i<4;++i)bank[i]=Byte(uint8_t("FSB4"[i]));
  Put32(bank,4,1);Put32(bank,8,80);Put32(bank,12,8);
  Put16(bank,48,80);Put32(bank,48+32,2);Put32(bank,48+36,8);
  Put32(bank,48+48,0x150u); // signed, 16-bit, stereo
  Put32(bank,48+52,48000);Put16(bank,48+62,2);
  const std::array<uint8_t,8> raw{0,0,232,3,24,252,0,0};
  for(size_t i=0;i<raw.size();++i)bank[128+i]=Byte(raw[i]);
  auto decoded=DecodeFsb4Pcm16(bank,0);
  assert(decoded && decoded->sample_rate==48000 &&
         decoded->channels==2 && decoded->pcm.size()==4);
  assert(decoded->pcm[0]==0 && decoded->pcm[1]==1000 &&
         decoded->pcm[2]==-1000 && decoded->pcm[3]==0);
  assert(!DecodeFsb4Pcm16(bank,1));
  // Big-endian source PCM requires real byte conversion.
  Put32(bank,20,8);
  for(unsigned i=0;i<8;i+=2)std::swap(bank[128+i],bank[129+i]);
  decoded=DecodeFsb4Pcm16(bank,0);
  assert(decoded && decoded->pcm[1]==1000 && decoded->pcm[2]==-1000);
  Put32(bank,20,0);
  for(unsigned i=0;i<8;i+=2)std::swap(bank[128+i],bank[129+i]);
  // Never send compressed XMA or unsigned samples directly to an audio device.
  Put32(bank,48+48,0x01000150u);
  assert(!DecodeFsb4Pcm16(bank,0));
  Put32(bank,48+48,0x1d0u); // signed PCM plus unsigned flag
  assert(!DecodeFsb4Pcm16(bank,0));
  Put32(bank,48+48,0x150u);
  Put32(bank,12,7);
  assert(!DecodeFsb4Pcm16(bank,0));
  Put32(bank,12,8);
  Put32(bank,8,64);
  assert(!DecodeFsb4Pcm16(bank,0));
  Put32(bank,8,80);
  Put32(bank,20,2); // unknown basic-header directory layout
  assert(!DecodeFsb4Pcm16(bank,0));
  Put32(bank,20,0);
  bank[3]=Byte{'5'};
  assert(!DecodeFsb4Pcm16(bank,0));
  std::cout << "Original FSB4 PCM16 to native audio contract PASS\n";
}
