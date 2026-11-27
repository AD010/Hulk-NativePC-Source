#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace hulk::eor::xdk {
// Source RECT and destination POINT in original guest texels. For the
// original color-only, level-zero, slice-zero Resolve ABI.
struct OriginalResolveRegion {
  std::array<int32_t,4> source{};
  std::array<int32_t,2> destination{};
  bool IsFull(uint32_t w,uint32_t h) const noexcept {
    return source[0]==0 && source[1]==0 && source[2]==int64_t(w) &&
           source[3]==int64_t(h) && destination[0]==0 && destination[1]==0;
  }
};
inline bool ValidOriginalResolveRegion(const OriginalResolveRegion& r,
                                       uint32_t sw,uint32_t sh,
                                       uint32_t dw,uint32_t dh) noexcept {
  const auto& s=r.source;const auto& d=r.destination;
  if(!sw || !sh || !dw || !dh || s[0]<0 || s[1]<0 ||
     s[2]<=s[0] || s[3]<=s[1] || s[2]>int64_t(sw) ||
     s[3]>int64_t(sh) || d[0]<0 || d[1]<0 ||
     int64_t(d[0])+s[2]-s[0]>dw || int64_t(d[1])+s[3]-s[1]>dh)return false;
  return true;
}
// Map the edge of a half-open source texel interval to the physical pixel grid.
// A pixel centered exactly on an edge belongs to the interval starting there.
inline int32_t MapOriginalTexelEdge(int32_t edge,uint32_t original,uint32_t host) noexcept {
  const uint64_t n=uint64_t(edge)*host;
  return static_cast<int32_t>(n/original+((n%original)*2u>original?1u:0u));
}
inline std::optional<OriginalResolveRegion> MapOriginalResolveRegion(
    const OriginalResolveRegion& r,uint32_t sw,uint32_t sh,
    uint32_t dw,uint32_t dh,uint32_t host_sw,uint32_t host_sh,
    uint32_t host_dw,uint32_t host_dh) noexcept {
  if(!ValidOriginalResolveRegion(r,sw,sh,dw,dh) || !host_sw || !host_sh ||
     !host_dw || !host_dh || host_sw>INT32_MAX || host_sh>INT32_MAX ||
     host_dw>INT32_MAX || host_dh>INT32_MAX)return {};
  // At a noninteger host scale (e.g. 720 -> 1600), an original
  // eight-texel-aligned rectangle need not land on integer host edges.
  // Use the same half-open pixel-center convention as the native viewport.
  // A plain GPU copy is exact only if source and destination grids have
  // equal scale AND the translation is an integral number of host pixels.
  // Reject differing phases rather than silently resampling.
  const auto phase=[](int32_t from,int32_t to,uint32_t src,uint32_t dst,
                      uint32_t host_src,uint32_t host_dst) {
    if(uint64_t(host_src)*dst!=uint64_t(host_dst)*src)return false;
    const int64_t shift=int64_t(to)-from;
    return (shift*int64_t(host_src))%src==0;
  };
  if(!phase(r.source[0],r.destination[0],sw,dw,host_sw,host_dw) ||
     !phase(r.source[1],r.destination[1],sh,dh,host_sh,host_dh))return {};
  OriginalResolveRegion result;
  for(unsigned i=0;i<4;++i) {
    result.source[i]=MapOriginalTexelEdge(r.source[i],i%2?sh:sw,i%2?host_sh:host_sw);
  }
  for(unsigned i=0;i<2;++i) {
    const auto v=MapOriginalTexelEdge(r.destination[i],i?dh:dw,i?host_dh:host_dw);
    result.destination[i]=v;
    const auto end=int64_t(r.destination[i])+r.source[i+2]-r.source[i];
    if(end>INT32_MAX)return {};
    const auto mapped_end=MapOriginalTexelEdge(static_cast<int32_t>(end),i?dh:dw,i?host_dh:host_dw);
    if(mapped_end-v!=result.source[i+2]-result.source[i])return {};
  }
  return ValidOriginalResolveRegion(result,host_sw,host_sh,host_dw,host_dh)?
      std::optional<OriginalResolveRegion>(result):std::nullopt;
}
}
