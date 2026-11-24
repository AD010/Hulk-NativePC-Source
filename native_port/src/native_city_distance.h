// Original USA EoR Levels QuickData runtime view. No modified ARC/IND on disk.
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hulk::city {
inline constexpr std::size_t kLevelsOffset=1372596u;
inline constexpr std::size_t kLevelsBytes=7804u;
inline constexpr std::size_t kZoneBufferInLevels=0x168u;
inline constexpr std::size_t kFarPlaneInLevels=0x1b4u;
inline constexpr uint64_t kOriginalLevelsFingerprint=0x91814db56ecea4d9ull;
inline constexpr float kOriginalFarPlane=2000.0f;
inline constexpr float kOriginalZoneBuffer=50.0f;

inline uint64_t Fingerprint(std::span<const uint8_t> bytes) noexcept {
  uint64_t hash=0xcbf29ce484222325ull;
  for(uint8_t byte:bytes)hash=(hash^byte)*0x100000001b3ull;
  return hash;
}
inline std::array<uint8_t,4> BigFloat(float value) noexcept {
  const auto bits=std::bit_cast<uint32_t>(value);
  return {uint8_t(bits>>24),uint8_t(bits>>16),uint8_t(bits>>8),uint8_t(bits)};
}
inline bool Intersects(std::size_t offset,std::size_t bytes,
                       std::size_t field) noexcept {
  return bytes && offset<=field+3u &&
      (offset>=field || bytes>field-offset);
}
inline bool IsTargetRead(std::size_t offset,std::size_t bytes) noexcept {
  return Intersects(offset,bytes,kLevelsOffset+kFarPlaneInLevels) ||
         Intersects(offset,bytes,kLevelsOffset+kZoneBufferInLevels);
}
inline bool VerifiedLevels(std::span<const uint8_t> source) noexcept {
  if(source.size()!=kLevelsBytes ||
     Fingerprint(source)!=kOriginalLevelsFingerprint)return false;
  const auto far=BigFloat(kOriginalFarPlane);
  const auto buffer=BigFloat(kOriginalZoneBuffer);
  for(std::size_t i=0;i<4;++i)
    if(source[kFarPlaneInLevels+i]!=far[i] ||
       source[kZoneBufferInLevels+i]!=buffer[i])return false;
  return true;
}
// Patch just the intersection of a real successful file read, including
// split/unaligned reads. First validate every affected returned byte so a
// changed file cannot result in a half-committed patch within this read.
inline bool ApplyToRead(std::span<uint8_t> returned,std::size_t offset,
                        std::span<const uint8_t> source,float far,float buffer) noexcept {
  if(!VerifiedLevels(source) || !std::isfinite(far) || !std::isfinite(buffer) ||
     far<kOriginalFarPlane || far>10000.0f ||
     buffer<kOriginalZoneBuffer || buffer>750.0f ||
     !IsTargetRead(offset,returned.size()))return false;
  const std::array fields{kFarPlaneInLevels,kZoneBufferInLevels};
  const std::array values{BigFloat(far),BigFloat(buffer)};
  for(std::size_t f=0;f<fields.size();++f) {
    for(std::size_t i=0;i<4;++i) {
      const auto absolute=kLevelsOffset+fields[f]+i;
      if(absolute<offset || absolute-offset>=returned.size())continue;
      if(returned[absolute-offset]!=source[fields[f]+i])return false;
    }
  }
  for(std::size_t f=0;f<fields.size();++f)
    for(std::size_t i=0;i<4;++i) {
      const auto absolute=kLevelsOffset+fields[f]+i;
      if(absolute>=offset && absolute-offset<returned.size())
        returned[absolute-offset]=values[f][i];
    }
  return true;
}
} // namespace hulk::city
