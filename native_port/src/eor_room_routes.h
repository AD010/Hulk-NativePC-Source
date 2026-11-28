#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace hulk::eor::audio {
// Positive sends retain their own room and gain. Zero sends need no DSP.
// A bounded graph must reject overflow rather than merge distinct rooms.
template<class Room,size_t Capacity=8>
struct NativeRoomRouteSet {
  struct Route {uint32_t owner=0;float gain=0;Room room{};};
  std::array<Route,Capacity> routes{};
  size_t count=0;
  bool SameTopology(const NativeRoomRouteSet& other) const noexcept {
    if(count>Capacity||other.count!=count)return false;
    for(size_t i=0;i<count;++i)if(routes[i].owner!=other.routes[i].owner)return false;
    return true;
  }
  template<class Validate>
  bool Valid(Validate&& validate) const noexcept {
    if(count>Capacity)return false;
    for(size_t i=0;i<count;++i){
      const auto& r=routes[i];
      if(!r.owner||(i&&routes[i-1].owner>=r.owner)||!std::isfinite(r.gain)||r.gain<=0||r.gain>1||!validate(r.room))return false;
    }
    return true;
  }
};
template<class Room,size_t Capacity=8,class Sends,class Validate>
std::optional<NativeRoomRouteSet<Room,Capacity>> PlanNativeRoomRoutes(const Sends& sends,Validate&& validate) noexcept {
  if(sends.count>sends.values.size())return {};
  NativeRoomRouteSet<Room,Capacity> out;
  for(size_t i=0;i<sends.count;++i){
    const auto& s=sends.values[i];
    if(!s.owner||!std::isfinite(s.gain)||s.gain<0||s.gain>1)return {};
    for(size_t j=0;j<i;++j)if(sends.values[j].owner==s.owner)return {};
    if(s.gain==0)continue;
    if(!s.room||!validate(*s.room)||out.count==Capacity)return {};
    out.routes[out.count++]={s.owner,s.gain,*s.room};
  }
  std::sort(out.routes.begin(),out.routes.begin()+out.count,[](const auto& a,const auto& b){return a.owner<b.owner;});
  return out;
}
} // namespace hulk::eor::audio
