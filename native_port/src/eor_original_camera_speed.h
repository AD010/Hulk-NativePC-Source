#pragma once
#include "eor_original_pause_vm.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace hulk::eor::vm {
struct CameraSpeedMethod {
  uint32_t method,thunk,length,global;
  uint64_t body_hash,thunk_hash;
  bool horizontal,setter;
};
inline constexpr std::array<CameraSpeedMethod,4> kCameraSpeedMethods{{
  {0x82339C18u,0x8211D2C0u,120,0x82E53B48u,0x8c23982666f12264ull,0x5b34a55d22f7d207ull,true,true},
  {0x82339C90u,0x8212BF50u,120,0x82E53B44u,0x5992eb39c0060110ull,0x1b88706310a2631full,false,true},
  {0x82338AE8u,0x8216D270u,56,0x82E53B48u,0x0f520da6080288f5ull,0x91cb8af71f5d6765ull,true,false},
  {0x82338B20u,0x821219E0u,56,0x82E53B44u,0x4f5891214625f859ull,0x7bf4555420501d7cull,false,false}
}};
struct CameraSpeedEvent {const CameraSpeedMethod* method;uint32_t value,output;};
template<class Read>
std::optional<uint32_t> CameraSpeedWord(uint32_t base,uint32_t offset,Read& read) noexcept {
  if(!base || uint64_t(base)+offset>UINT32_MAX-3u)return {};
  std::array<uint8_t,4> b{};
  if(!read(base+offset,b.data(),b.size()))return {};
  return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
}
template<class Read>
std::optional<CameraSpeedEvent> CaptureCameraSpeed(uint32_t method,uint32_t lr,
    uint32_t vm,uint32_t dispatcher_vm,uint32_t descriptor,uint32_t context,Read&& read) noexcept {
  const CameraSpeedMethod* spec=nullptr;
  for(const auto& entry:kCameraSpeedMethods)if(entry.method==method)spec=&entry;
  if(!spec)return {};
  auto pin=[&](uint32_t address,size_t n,uint64_t expected){
    std::array<uint8_t,120> bytes{};
    if(n>bytes.size() || !read(address,bytes.data(),n))return false;
    uint64_t h=0xcbf29ce484222325ull;
    for(size_t i=0;i<n;++i)h=(h^bytes[i])*0x100000001b3ull;
    return h==expected;
  };
  if(!pin(spec->method,spec->length,spec->body_hash) ||
     !pin(spec->thunk,16,spec->thunk_hash))return {};
  auto page=ResolveNativeOwner(lr,vm,dispatcher_vm,descriptor,context,read,spec->thunk).page;
  if(!IsOriginalSettingsRoot(page) && page!=PausePage::GameSettings && page!=PausePage::CameraSettings)return {};
  const auto frame=CameraSpeedWord(vm,0,read);
  if(!frame)return {};
  if(spec->setter) {
    if(CameraSpeedWord(0x82002454u,0,read)!=0x3c23d70au)return {};
    const auto input=CameraSpeedWord(*frame,0,read);
    const auto nested=input?CameraSpeedWord(*input,8,read):std::nullopt;
    const auto arg=nested?CameraSpeedWord(*nested,0,read):std::nullopt;
    const auto value=arg?CameraSpeedWord(*arg,12,read):std::nullopt;
    if(!value || *value<1 || *value>300)return {};
    return CameraSpeedEvent{spec,*value,0};
  }
  if(CameraSpeedWord(0x82002B40u,0,read)!=0x42c80000u ||
     CameraSpeedWord(0x82000A44u,0,read)!=0x3f000000u)return {};
  const auto out=CameraSpeedWord(*frame,4,read);
  if(!out || !*out || *out>UINT32_MAX-15u)return {};
  return CameraSpeedEvent{spec,0,*out+12u};
}
template<class Read>
bool CameraSpeedApplied(const CameraSpeedEvent& e,Read&& read) noexcept {
  if(!e.method || !e.method->setter || e.value<1 || e.value>300)return false;
  const auto bits=CameraSpeedWord(e.method->global,0,read);
  return bits && std::abs(std::bit_cast<float>(*bits)-float(e.value)*0.01f)<0.000001f;
}
// Map the full original slider to the existing PC range, keeping 100=1x.
inline double CameraSpeedMultiplier(uint32_t v) noexcept {
  v=std::clamp(v,1u,300u);
  return v<=100?0.25+0.75*double(v-1)/99.0:1.0+3.0*double(v-100)/200.0;
}
inline uint32_t CameraSpeedSlider(double v) noexcept {
  if(!std::isfinite(v))return 100;
  v=std::clamp(v,0.25,4.0);
  return uint32_t(std::lround(v<=1.0?1.0+(v-0.25)*99.0/0.75:100.0+(v-1.0)*200.0/3.0));
}
} // namespace hulk::eor::vm
