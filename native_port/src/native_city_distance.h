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
// Original SkyManager day/night nested EoR float properties, authenticated
// independently from Levels. No map UI or other environment is replaced.
inline constexpr std::size_t kSkyOffset=21411907u;
inline constexpr std::size_t kSkyBytes=4973u;
inline constexpr uint64_t kOriginalSkyFingerprint=0xc640d45951f76361ull;
inline constexpr std::array<std::size_t,2> kFogFarInSky{0xdb4u,0x123bu};

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
inline bool IsSkyFogRead(std::size_t offset,std::size_t bytes) noexcept {
  for(auto field:kFogFarInSky)
    if(Intersects(offset,bytes,kSkyOffset+field))return true;
  return false;
}
inline bool ApplySkyFogToRead(std::span<uint8_t> returned,std::size_t offset,
                             std::span<const uint8_t> source,float far) noexcept {
  if(source.size()!=kSkyBytes || Fingerprint(source)!=kOriginalSkyFingerprint ||
     !std::isfinite(far) || far<kOriginalFarPlane || far>10000.0f ||
     !IsSkyFogRead(offset,returned.size()))return false;
  const auto old=BigFloat(kOriginalFarPlane),next=BigFloat(far);
  for(auto field:kFogFarInSky)
    for(std::size_t i=0;i<4;++i) {
      if(source[field+i]!=old[i])return false;
      const auto absolute=kSkyOffset+field+i;
      if(absolute>=offset && absolute-offset<returned.size() &&
         returned[absolute-offset]!=old[i])return false;
    }
  for(auto field:kFogFarInSky)
    for(std::size_t i=0;i<4;++i) {
      const auto absolute=kSkyOffset+field+i;
      if(absolute>=offset && absolute-offset<returned.size())
        returned[absolute-offset]=next[i];
    }
  return true;
}
// Exact typed EoR LODDistance fields from the original city block base
// and two original rooftop structure prototypes. All other object fields
// (destruction, collision, mission and sky state) remain unchanged.
struct LodRecord {
  std::size_t offset,size,field;
  uint64_t fingerprint;
  float original_distance;
};
inline constexpr std::array<LodRecord,3> kLodRecords{{
    {9437140u,648u,602u,0x4050f81ffff70154ull,200.0f},
    {9097777u,238u,136u,0xf3fa0da3de4e767eull,300.0f},
    {9655909u,238u,136u,0x66bad8a4aab32b1ull,500.0f}}};
inline constexpr std::size_t kMaxLodRecordBytes=648u;
inline const LodRecord* FindLodRead(std::size_t offset,std::size_t bytes) noexcept {
  for(const auto& record:kLodRecords)
    if(Intersects(offset,bytes,record.offset+record.field))return &record;
  return nullptr;
}
inline bool ApplyLodToRead(std::span<uint8_t> returned,std::size_t offset,
                           std::span<const uint8_t> source,
                           const LodRecord& record,float scale) noexcept {
  if(source.size()!=record.size || Fingerprint(source)!=record.fingerprint ||
     !std::isfinite(scale) || scale<1.0f || scale>5.0f ||
     record.field>source.size() || source.size()-record.field<4u ||
     !Intersects(offset,returned.size(),record.offset+record.field))return false;
  const auto original=BigFloat(record.original_distance);
  const auto replacement=BigFloat(record.original_distance*scale);
  for(std::size_t i=0;i<4;++i) {
    if(source[record.field+i]!=original[i])return false;
    const auto absolute=record.offset+record.field+i;
    if(absolute>=offset && absolute-offset<returned.size() &&
       returned[absolute-offset]!=original[i])return false;
  }
  for(std::size_t i=0;i<4;++i) {
    const auto absolute=record.offset+record.field+i;
    if(absolute>=offset && absolute-offset<returned.size())
      returned[absolute-offset]=replacement[i];
  }
  return true;
}
} // namespace hulk::city
