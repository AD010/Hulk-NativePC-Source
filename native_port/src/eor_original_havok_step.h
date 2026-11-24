#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hulk::eor::havok {
enum class Simulation : uint8_t { Discrete, Continuous, Multithreaded };
struct Step {
  uint32_t world=0,simulation=0,scheduler=0;
  Simulation kind=Simulation::Discrete;
  double seconds=0;
  float current_time=0,psi_time=0,previous_delta=0;
  bool split=false;
};
// Exact USA Havok 5.1 source fingerprints. The threaded scheduler calls
// the split prepare thunk with world in r3, delta in f1, owner in r31.
// Never treat a different call site or an unverified heap object as a clock.
template<class Read>
std::optional<Step> CaptureStep(uint32_t method,uint32_t lr,uint32_t r3,
    uint32_t r31,double f1,Read&& read) noexcept {
  const bool split=method==0x82508F60u;
  if((!split && method!=0x82509C18u) || !r3 ||
     !std::isfinite(f1) || f1<0 || f1>1)return {};
  auto word=[&](uint32_t address,uint32_t off)->std::optional<uint32_t>{
    if(!address || uint64_t(address)+off>UINT32_MAX-3u)return {};
    std::array<uint8_t,4> v{};
    if(!read(address+off,v.data(),v.size()))return {};
    return uint32_t(v[0])<<24|uint32_t(v[1])<<16|uint32_t(v[2])<<8|v[3];
  };
  auto pin=[&](uint32_t address,size_t size,uint64_t expected){
    std::array<uint8_t,256> v{};
    if(size>v.size() || !read(address,v.data(),size))return false;
    uint64_t h=0xcbf29ce484222325ull;
    for(size_t i=0;i<size;++i)h=(h^v[i])*0x100000001b3ull;
    return h==expected;
  };
  if(!pin(0x82051EF0u,12,0x6a67acdf56cabb72ull) ||
     !pin(method,split?8u:256u,split?0xd79e5d224e9e743bull:0x7c923cfb4c5a445aull))return {};
  const auto world_vt=word(r3,0),sim=word(r3,8);
  if(!world_vt || *world_vt!=0x82051EF0u || !sim || !*sim)return {};
  const auto vt=word(*sim,0),owner=word(*sim,8);
  if(!vt || !owner || *owner!=r3)return {};
  Simulation kind;
  switch(*vt) {
    case 0x82050D94u:
      if(!pin(*vt,48,0x940d5395c982d6c4ull))return {};
      kind=Simulation::Discrete;break;
    case 0x82053104u:
      if(!pin(*vt,48,0x0de56fd74f5f3252ull))return {};
      kind=Simulation::Continuous;break;
    case 0x8205272Cu:
      if(!pin(*vt,64,0x084ae6b7647724eeull))return {};
      kind=Simulation::Multithreaded;break;
    default:return {};
  }
  if(split) {
    if(kind!=Simulation::Multithreaded ||
       !pin(0x825314B8u,256,0xb51a339d98dadd21ull))return {};
    if(lr==0x8296B898u) {
      if(!pin(0x8296B88Cu,12,0x8b05d8e3fb4196c4ull))return {};
    } else if(lr==0x8296B97Cu) {
      if(!pin(0x8296B970u,12,0x8afbb4e3fb390513ull))return {};
    } else return {};
    const auto w=word(r31,0),d=word(r31,4);
    if(!w || *w!=r3 || !d || double(std::bit_cast<float>(*d))!=f1)return {};
  } else {
    const auto target=word(*vt,12);
    const bool multi=kind==Simulation::Multithreaded;
    if(!target || *target!=(multi?0x82531D30u:0x82516090u) ||
       !pin(*target,multi?152u:164u,multi?0x4ac5137937d617beull:0x98177df947888e04ull))return {};
  }
  const auto time=word(*sim,16),psi=word(*sim,20),delta=word(*sim,24);
  if(!time || !psi || !delta)return {};
  const float t=std::bit_cast<float>(*time),p=std::bit_cast<float>(*psi),
      d=std::bit_cast<float>(*delta);
  if(!std::isfinite(t) || !std::isfinite(p) || !std::isfinite(d))return {};
  return Step{r3,*sim,split?r31:0u,kind,f1,t,p,d,split};
}
} // namespace hulk::eor::havok
