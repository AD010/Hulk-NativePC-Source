#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace hulk::eor::audio {
inline constexpr uint32_t kUsaEventSystemUpdate=0x82841DB0u;
inline constexpr uint32_t kUsaEventSystemVtable=0x82089600u;
inline constexpr uint64_t kUsaEventSystemUpdateHash=0xe5d96c39496dd8feull;
template<class Read>
std::optional<uint32_t> ReadFmodBeWord(uint32_t address,Read&& read) noexcept {
  std::array<std::byte,4> b{};
  if(address>UINT32_MAX-3u||!read(address,b.data(),b.size()))return {};
  uint32_t word=0;for(auto v:b)word=(word<<8)|std::to_integer<uint32_t>(v);
  return word;
}
struct FmodUpdateStamp {uint32_t owner,clock;};
template<class Read>
std::optional<FmodUpdateStamp> CaptureFmodUpdateStamp(uint32_t owner,Read&& read) noexcept {
  if(!owner||owner>UINT32_MAX-163u||(owner&3u))return {};
  const auto table=ReadFmodBeWord(owner,read);
  if(table!=kUsaEventSystemVtable||ReadFmodBeWord(kUsaEventSystemVtable+8u,read)!=kUsaEventSystemUpdate)return {};
  const auto enabled=ReadFmodBeWord(owner+160u,read);
  const auto stamp=ReadFmodBeWord(owner+28u,read);
  if(!enabled||!*enabled||!stamp)return {};
  return FmodUpdateStamp{owner,*stamp};
}
template<class Read>
bool VerifyFmodUpdateBody(Read&& read) noexcept {
  std::array<std::byte,192> b{};
  if(!read(kUsaEventSystemUpdate,b.data(),b.size()))return false;
  uint64_t hash=0xcbf29ce484222325ull;
  for(auto v:b)hash=(hash^std::to_integer<uint8_t>(v))*0x100000001b3ull;
  return hash==kUsaEventSystemUpdateHash;
}
// USA 82841DF4..82841E0C uses current-old, or current on wrap.
// First initialization samples the timer twice inside the original call;
// the first sample is not observable at entry, so do not invent elapsed time.
inline std::optional<uint32_t> FmodUpdateDelta(FmodUpdateStamp before,FmodUpdateStamp after,uint32_t result) noexcept {
  if(result||!before.owner||before.owner!=after.owner)return {};
  if(!before.clock)return 0u;
  return after.clock<before.clock?after.clock:after.clock-before.clock;
}
} // namespace hulk::eor::audio
