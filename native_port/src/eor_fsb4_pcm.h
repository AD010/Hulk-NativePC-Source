#pragma once
// Decode source-owned FMOD Ex FSB4 *PCM16* entries for the native x64
// XAudio2 backend. Compressed XMA/IMA/MPEG banks are deliberately rejected:
// reading their sample bytes as PCM would produce garbage/noise.
#include "eor_native_audio_xaudio2.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hulk::eor::audio {
struct DecodedFsbPcm16 {
  uint32_t sample_rate=0;
  uint16_t channels=0;
  std::vector<int16_t> pcm;
};
inline uint16_t FsbLe16(std::span<const std::byte> s,size_t at) noexcept {
  return uint16_t(std::to_integer<uint8_t>(s[at])) |
         (uint16_t(std::to_integer<uint8_t>(s[at+1]))<<8);
}
inline uint32_t FsbLe32(std::span<const std::byte> s,size_t at) noexcept {
  return uint32_t(FsbLe16(s,at)) | (uint32_t(FsbLe16(s,at+2))<<16);
}
inline std::optional<DecodedFsbPcm16> DecodeFsb4Pcm16(
    std::span<const std::byte> fsb,uint32_t ordinal) {
  constexpr uint64_t kMaxBank=64ull<<20;
  if(fsb.size()<48 || fsb.size()>kMaxBank ||
     std::to_integer<uint8_t>(fsb[0])!='F' ||
     std::to_integer<uint8_t>(fsb[1])!='S' ||
     std::to_integer<uint8_t>(fsb[2])!='B' ||
     std::to_integer<uint8_t>(fsb[3])!='4')return {};
  const uint32_t count=FsbLe32(fsb,4);
  const uint32_t headers=FsbLe32(fsb,8);
  const uint32_t bytes=FsbLe32(fsb,12);
  const uint32_t flags=FsbLe32(fsb,20);
  if(!count || count>4096 || ordinal>=count ||
     (flags&2u) || headers<uint64_t(count)*80u ||
     uint64_t(48)+headers+bytes>fsb.size())return {};
  size_t cursor=48;
  uint64_t data_offset=uint64_t(48)+headers;
  for(uint32_t i=0;i<count;++i) {
    if(cursor+80u>uint64_t(48)+headers)return {};
    const uint16_t size=FsbLe16(fsb,cursor);
    if(size<80 || uint64_t(cursor)+size>uint64_t(48)+headers)return {};
    const auto frame_count=FsbLe32(fsb,cursor+32);
    const auto payload_bytes=FsbLe32(fsb,cursor+36);
    const auto mode=FsbLe32(fsb,cursor+48);
    const auto rate=FsbLe32(fsb,cursor+52);
    const auto channels=FsbLe16(fsb,cursor+62);
    if(uint64_t(data_offset)+payload_bytes>uint64_t(48)+headers+bytes)return {};
    if(i==ordinal) {
      // FSOUND_16BITS, no unsigned/Delta/IT/IMA/VAG/XMA/GCADPCM.
      // A supported payload must contain exactly frame_count*channels
      // little-endian 16-bit samples, not a padded compressed stream.
      constexpr uint32_t unsupported=0x00000e80u|0x00400000u|
          0x00800000u|0x01000000u|0x02000000u|0x04000000u;
      if(!(mode&0x10u) || (mode&unsupported) ||
         !frame_count || (channels!=1 && channels!=2) ||
         frame_count>1048576u/channels ||
         uint64_t(frame_count)*channels*2u!=payload_bytes)
        return {};
      DecodedFsbPcm16 result;
      result.sample_rate=rate;
      result.channels=channels;
      result.pcm.reserve(size_t(frame_count)*channels);
      const bool big_endian=(flags&0x8u)!=0;
      for(uint32_t n=0;n<frame_count*channels;++n) {
        const auto a=std::to_integer<uint8_t>(fsb[size_t(data_offset)+2u*n]);
        const auto b=std::to_integer<uint8_t>(fsb[size_t(data_offset)+2u*n+1u]);
        const uint16_t word=big_endian?(uint16_t(a)<<8|b):
                                  (uint16_t(b)<<8|a);
        result.pcm.push_back(static_cast<int16_t>(
            word<0x8000u?int32_t(word):int32_t(word)-65536));
      }
      if(!ValidPcm16(result.pcm,rate,channels))return {};
      return result;
    }
    cursor+=size;
    data_offset+=payload_bytes;
  }
  return {};
}
} // namespace hulk::eor::audio
