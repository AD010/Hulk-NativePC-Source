#include "../native_port/src/eor_fsb4_pcm.h"
#include <algorithm>
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
  Put32(bank,20,2); // BASICHEADERS with one full header is also valid
  assert(DecodeFsb4Pcm16(bank,0));
  Put32(bank,20,0x40); // one sample requires no alignment gap
  assert(DecodeFsb4Pcm16(bank,0));
  Put32(bank,20,0);
  bank[3]=Byte{'5'};
  assert(!DecodeFsb4Pcm16(bank,0));
  bank[3]=Byte{'4'};
  // Real FSB4 BASICHEADERS layout: one 80-byte sample header followed
  // by eight bytes (frames/data) for each subsequent same-format sample.
  std::vector<Byte> basic(48u+88u+16u,Byte{0});
  std::copy(bank.begin(),bank.begin()+128,basic.begin());
  Put32(basic,4,2);Put32(basic,8,88);Put32(basic,12,16);
  Put32(basic,20,2);
  Put32(basic,128,2);Put32(basic,132,8);
  for(size_t i=0;i<raw.size();++i) {
    basic[136+i]=Byte(raw[i]);
    basic[144+i]=Byte(raw[i]);
  }
  decoded=DecodeFsb4Pcm16(basic,1);
  assert(decoded && decoded->sample_rate==48000 &&
         decoded->channels==2 && decoded->pcm[1]==1000);
  // FMOD 0x40 aligns each subsequent FSB4 sample to the next 32-byte
  // absolute file offset. Initial payload starts at 136, first ends
  // at 144 and the second sample begins at 160 (16 bytes of padding).
  basic.resize(168,Byte{0});
  Put32(basic,12,32);
  Put32(basic,20,0x42u); // BASICHEADERS | aligned
  for(size_t i=0;i<raw.size();++i)basic[160+i]=Byte(raw[i]);
  decoded=DecodeFsb4Pcm16(basic,1);
  assert(decoded && decoded->pcm[1]==1000 && decoded->pcm[2]==-1000);
  basic.resize(159);
  assert(!DecodeFsb4Pcm16(basic,1));
  basic.resize(168);
  Put32(basic,132,0xffffffffu);
  assert(!DecodeFsb4Pcm16(basic,1));

  // Unsigned 8-bit FMOD samples must be re-centered before XAudio2 PCM16.
  // Two stereo frames, with explicitly declared FSOUND_UNSIGNED.
  std::vector<Byte> pcm8(48u+80u+4u,Byte{0});
  std::copy(bank.begin(),bank.begin()+128,pcm8.begin());
  Put32(pcm8,4,1);Put32(pcm8,8,80);Put32(pcm8,12,4);
  Put32(pcm8,48+32,2);Put32(pcm8,48+36,4);
  Put32(pcm8,48+48,0xC8u); // FSOUND_8BITS | STEREO | UNSIGNED
  pcm8[128]=Byte{0x80};pcm8[129]=Byte{0x00};
  pcm8[130]=Byte{0x7f};pcm8[131]=Byte{0xff};
  decoded=DecodeFsb4Pcm16(pcm8,0);
  assert(decoded && decoded->pcm.size()==4 &&
         decoded->pcm[0]==0 && decoded->pcm[1]==-32768 &&
         decoded->pcm[2]==-256 && decoded->pcm[3]==32512);

  // Signed PCM8 with the same bytes has a different, verified value map.
  Put32(pcm8,48+48,0x148u); // FSOUND_8BITS | STEREO | SIGNED
  decoded=DecodeFsb4Pcm16(pcm8,0);
  assert(decoded && decoded->pcm[0]==-32768 &&
         decoded->pcm[1]==0 && decoded->pcm[2]==32512 &&
         decoded->pcm[3]==-256);

  Put32(pcm8,48+48,0x1C8u); // ambiguous SIGNED | UNSIGNED
  assert(!DecodeFsb4Pcm16(pcm8,0));
  Put32(pcm8,48+48,0x01000148u); // XMA must not be played as PCM8
  assert(!DecodeFsb4Pcm16(pcm8,0));
  std::cout << "Original FSB4 PCM8/16 to native audio contract PASS\n";
}
