#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace hulk::eor::world {
inline constexpr uint32_t kOriginalFogState=0x82e78040u;
inline constexpr uint32_t kFogRenderCopyA=1712u,kFogRenderCopyB=1920u;
struct FogRange {uint32_t near_bits=0,far_bits=0;bool operator==(const FogRange&)const noexcept=default;};
inline std::optional<FogRange> ScaleOriginalFogCopies(FogRange source,FogRange a,FogRange b,int scale) noexcept {
  if(scale<=1||scale>4||source!=a||source!=b)return {};
  const float near=std::bit_cast<float>(source.near_bits),far=std::bit_cast<float>(source.far_bits);
  if(!std::isfinite(near)||!std::isfinite(far)||near<0.f||far<=near||far>1.e7f)return {};
  return FogRange{std::bit_cast<uint32_t>(near*float(scale)),std::bit_cast<uint32_t>(far*float(scale))};
}
// Only the three source callsites building a temporary render state on
// their own stack are authorized. No persistent atmosphere/physics writes.
template<class Read>
bool VerifyFogRenderCall(uint32_t lr,uint32_t stack,uint32_t owner,Read&& read) noexcept {
  uint32_t offset=0;uint64_t caller=0;
  switch(lr){
    case 0x8229a5fcu:offset=80;caller=0xee8467ac398135c9ull;break;
    case 0x82305b80u:offset=96;caller=0xf8c70d54e747ec7cull;break;
    case 0x823539ecu:offset=80;caller=0xa6e39af015ea1607ull;break;
    default:return false;
  }
  if(!stack||(stack&15u)||uint64_t(stack)+offset!=owner||owner>UINT32_MAX-kFogRenderCopyB-7u)return false;
  const auto pin=[&](uint32_t addr,size_t size,uint64_t expected){
    std::array<std::byte,620> bytes{};
    if(size>bytes.size()||!read(addr,bytes.data(),size))return false;
    uint64_t h=0xcbf29ce484222325ull;
    for(size_t i=0;i<size;++i)h=(h^std::to_integer<uint8_t>(bytes[i]))*0x100000001b3ull;
    return h==expected;
  };
  return pin(lr-12u,12,caller)&&pin(0x825e7668u,620,0x1e9ef9fdc18baedfull);
}
} // namespace hulk::eor::world
