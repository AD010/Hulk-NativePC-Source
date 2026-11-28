#pragma once
// Source-authenticated outer EoR physics scheduler. The retail engine
// already uses a 60 Hz step and waits for ALL of its Havok worker jobs.
// Preserve bounded catch-up debt at the original game-owned accumulator;
// never replace the shared game delta or alter an in-flight worker step.
#include "eor_original_havok_step.h"
#include "eor_havok_carry.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

namespace hulk::eor::havok {
inline constexpr float kOriginalStep=std::bit_cast<float>(0x3c888889u);
struct CatchupPlan {float accumulator=0;uint32_t debt=0,extra=0;};
// Keep the original fixed step untouched. A destruction spike must not
// schedule seven Havok updates inside its next delayed frame. A maximum
// of four committed steps plus one possible original fractional carry
// spreads recovery over subsequent frames without dropping time or jobs.
inline constexpr uint32_t kCatchupMaxTotalSteps=5u;
inline std::optional<CatchupPlan> PlanOriginalCatchup(
    float elapsed,float step,float remainder,uint32_t debt) noexcept {
  // Zero or large discontinuities (pause/load/debugger break) never add
  // catch-up work. A corrupted clock is left entirely to the guest.
  if(!std::isfinite(elapsed) || elapsed<=0 || elapsed>0.25f ||
     step!=kOriginalStep || !std::isfinite(remainder) ||
     remainder<0 || remainder>=step || debt>120u)return {};
  const auto whole=static_cast<uint32_t>(std::floor(elapsed/step));
  const auto admitted=std::min(whole,3u);
  const auto pending=debt+whole-admitted;
  // Reserve one original fractional carry; never issue more than four
  // whole physics steps per host frame. Keep the remaining exact debt.
  const auto wanted=std::min(pending,kCatchupMaxTotalSteps-admitted-1u);
  // PPC single-precision rounding near two fractional steps can create
  // one additional carry. Validate the actual original loop and debit
  // only the number of extra steps proven to execute.
  const auto carry=AdmitOriginalCatchup(remainder,elapsed,step,whole,admitted,wanted,kCatchupMaxTotalSteps);
  if(!carry||pending-carry->extra>120u)return {};
  return CatchupPlan{carry->accumulator,pending-carry->extra,carry->extra};
}
// The retail scheduler calls its floor helper after its pre-physics callbacks
// and after reading the live elapsed/step. The same helper is used elsewhere
// in the game, so only this exact return address and quotient are admissible.
inline constexpr uint32_t kOriginalSchedulerFloorReturn=0x8261187cu;
inline bool MatchesOriginalSchedulerFloorInput(uint32_t lr,double quotient,
    float elapsed,float step) noexcept {
  return lr==kOriginalSchedulerFloorReturn &&
      std::isfinite(quotient) && std::isfinite(elapsed) &&
      elapsed>0 && elapsed<=0.25f && step==kOriginalStep &&
      quotient==static_cast<double>(elapsed/step);
}
struct OuterClock {
  uint32_t owner=0,world=0,simulation=0,scheduler=0;
  float elapsed=0,step=0,accumulator=0,simulation_time=0;
};
template<class Read>
std::optional<OuterClock> CaptureOuterClock(uint32_t owner,Read&& read) noexcept {
  auto pin=[&](uint32_t address,size_t size,uint64_t expected){
    std::array<uint8_t,284> v{};
    if(size>v.size() || !read(address,v.data(),size))return false;
    uint64_t h=0xcbf29ce484222325ull;
    for(size_t i=0;i<size;++i)h=(h^v[i])*0x100000001b3ull;
    return h==expected;
  };
  if(!owner || !pin(0x82611740u,40,0x3c2a2346f021cdc9ull) ||
     !pin(0x82611848u,284,0x0be7ba327ab8184bfull) ||
     !pin(0x82738B58u,60,0xb241b2feddc2c150ull) ||
     !pin(0x8267BBC8u,128,0x1c06e98d41f8865cull) ||
     !pin(0x8296B180u,128,0x5d1cfcf739311e39ull) ||
     !pin(0x8296B200u,88,0xfb59ee03a13e5ec6ull) ||
     !pin(0x826115D8u,164,0x089e1bf6bee9d309ull))return {};
  const auto word=[&](uint32_t base,uint32_t off)->std::optional<uint32_t>{
    if(!base || uint64_t(base)+off>UINT32_MAX-3u)return {};
    std::array<uint8_t,4> v{};
    if(!read(base+off,v.data(),v.size()))return {};
    return uint32_t(v[0])<<24|uint32_t(v[1])<<16|uint32_t(v[2])<<8|v[3];
  };
  const auto wrapper=word(owner,0),world=word(owner,4);
  const auto step=word(owner,20),accum=word(owner,24),elapsed=word(0x82E2C804u,0);
  uint8_t enabled=0;
  if(!wrapper || !world || !*world || !step || !accum || !elapsed ||
     !read(0x82E2D040u,&enabled,1) || enabled!=1)return {};
  const auto wrapper_world=word(*wrapper,0),scheduler=word(*wrapper,4);
  if(!wrapper_world || *wrapper_world!=*world || !scheduler || !*scheduler)return {};
  const auto scheduler_world=word(*scheduler,0),workers=word(*scheduler,328);
  uint8_t running=1;
  if(!scheduler_world || *scheduler_world!=*world ||
     !workers || !*workers || *workers>6 || *scheduler>UINT32_MAX-32u ||
     !read(*scheduler+32u,&running,1) || running)return {};
  const auto dt=std::bit_cast<float>(*step);
  const auto sim=CaptureStep(0x82509C18u,0,*world,0,dt,read);
  if(!sim || sim->kind!=Simulation::Multithreaded || dt!=kOriginalStep)return {};
  return OuterClock{owner,*world,sim->simulation,*scheduler,
       std::bit_cast<float>(*elapsed),dt,std::bit_cast<float>(*accum),sim->current_time};
}
} // namespace hulk::eor::havok
