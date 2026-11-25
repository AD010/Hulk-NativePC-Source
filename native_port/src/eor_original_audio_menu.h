#pragma once
// Original USA EoR volume VM handlers and the actual three-channel
// lock/unlock transaction. No mixer is substituted and no F10 dependency.
#include "eor_original_pause_vm.h"
#include <bit>
#include <cmath>
#include <cstdint>

namespace hulk::eor::vm {
enum class AudioChannel : uint8_t { Music,Sfx,Dialog,Lock };
struct AudioMenuMethod {
  uint32_t method,thunk,size;
  uint64_t body_hash,thunk_hash;
  AudioChannel channel;
};
inline constexpr std::array<AudioMenuMethod,4> kAudioMenuMethods{{
  {0x82339D20u,0x8218CF70u,108,0xb94e5ad803a0e9c5ull,0xebe8264ae35ae47eull,AudioChannel::Music},
  {0x82339D90u,0x821674F0u,152,0xa7b26a63978e8ebdull,0xcf14869fca7cbdeeull,AudioChannel::Sfx},
  {0x82339E28u,0x82139740u,132,0xba096ef6bf87099cull,0x27bbc70964ecfc69ull,AudioChannel::Dialog},
  {0x82339D08u,0x82153460u,24,0x87b5a61b5e45454bull,0x0dbce202485ba306ull,AudioChannel::Lock}
}};
inline constexpr uint32_t kOriginalVolumeLock=0x82E59CA0u;
inline constexpr uint32_t kOriginalActiveVolume=0x82E53B54u;
inline constexpr uint32_t kOriginalDeferredVolume=0x82E593CCu;
struct AudioMenuChange {AudioChannel channel;uint32_t value;bool was_locked;};
template<class Read>
std::optional<uint32_t> ReadAudioWord(uint32_t a,Read&& read) noexcept {
  std::array<uint8_t,4> b{};
  if(!a || a>UINT32_MAX-3u || !read(a,b.data(),b.size()))return {};
  return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
}
template<class Read>
std::optional<bool> ReadVolumeLock(Read&& read) noexcept {
  uint8_t b=0;
  if(!read(kOriginalVolumeLock,&b,1) || b>1)return {};
  return b!=0;
}
template<class Read>
std::optional<AudioMenuChange> CaptureAudioMenuChange(uint32_t method,uint32_t lr,
    uint32_t vm,uint32_t dispatcher_vm,uint32_t descriptor,uint32_t context,
    Read&& read) noexcept {
  const AudioMenuMethod* spec=nullptr;
  for(const auto& s:kAudioMenuMethods)if(s.method==method)spec=&s;
  if(!spec)return {};
  const auto pin=[&](uint32_t a,size_t n,uint64_t expected){
    std::array<uint8_t,200> bytes{};
    if(n>bytes.size() || !read(a,bytes.data(),n))return false;
    uint64_t h=0xcbf29ce484222325ull;
    for(size_t i=0;i<n;++i)h=(h^bytes[i])*0x100000001b3ull;
    return h==expected;
  };
  if(!pin(spec->thunk,16,spec->thunk_hash) ||
     !pin(spec->method,spec->size,spec->body_hash) ||
     ResolveNativeOwner(lr,vm,dispatcher_vm,descriptor,context,read,spec->thunk).page==PausePage::Unknown)return {};
  const auto lock=ReadVolumeLock(read);
  if(!lock)return {};
  const auto word=[&](uint32_t a,uint32_t offset)->std::optional<uint32_t>{
    if(!a || uint64_t(a)+offset>UINT32_MAX-3u)return {};
    return ReadAudioWord(a+offset,read);
  };
  const auto frame=word(vm,0);
  const auto input=frame?word(*frame,0):std::nullopt;
  const auto nested=input?word(*input,8):std::nullopt;
  const auto arg=nested?word(*nested,0):std::nullopt;
  if(!arg || !*arg || *arg>UINT32_MAX-12u)return {};
  if(spec->channel==AudioChannel::Lock) {
    uint8_t value=0;
    if(!pin(0x8218A0E0u,16,0x7ec891da6ad93ef4ull) ||
       !pin(0x82338B58u,200,0x88bd481b86dcbdd9ull) ||
       !read(*arg+12u,&value,1) || value>1 || bool(value)==*lock)return {};
    return AudioMenuChange{AudioChannel::Lock,value,*lock};
  }
  const auto value=word(*arg,12);
  const auto scale=ReadAudioWord(0x82002454u,read);
  if(!value || *value>100 || !scale || *scale!=0x3c23d70au)return {};
  return AudioMenuChange{spec->channel,*value,*lock};
}
template<class Read>
bool OriginalVolumeApplied(const AudioMenuChange& change,Read&& read) noexcept {
  if(change.channel==AudioChannel::Lock || change.value>100)return false;
  const auto lock=ReadVolumeLock(read);
  if(!lock || *lock!=change.was_locked)return false;
  const auto address=(*lock?kOriginalDeferredVolume:kOriginalActiveVolume)+
      4u*static_cast<uint32_t>(change.channel);
  const auto bits=ReadAudioWord(address,read);
  if(!bits)return false;
  const auto current=std::bit_cast<float>(*bits);
  return std::isfinite(current) &&
      std::abs(current-float(change.value)*0.01f)<0.000001f;
}
// Called only after the original lock callback copied active volumes to
// its own deferred storage. Preserve inherited channels and do not
// touch live mixer memory. The original unlock applies these settings.
struct DeferredVolumeRestore {
  std::array<uint8_t,12> before{},after{};
};
template<class Read>
std::optional<DeferredVolumeRestore> PlanDeferredVolumeRestore(
    const AudioMenuChange& change,const std::array<int32_t,3>& preferences,
    Read&& read) noexcept {
  if(change.channel!=AudioChannel::Lock || change.was_locked || change.value!=1)return {};
  const auto lock=ReadVolumeLock(read);
  if(!lock || !*lock)return {};
  DeferredVolumeRestore plan;
  if(!read(kOriginalDeferredVolume,plan.before.data(),12) ||
     !read(kOriginalActiveVolume,plan.after.data(),12) || plan.before!=plan.after)return {};
  for(size_t i=0;i<3;++i){
    const auto old=uint32_t(plan.before[i*4])<<24|uint32_t(plan.before[i*4+1])<<16|
        uint32_t(plan.before[i*4+2])<<8|plan.before[i*4+3];
    const auto value=std::bit_cast<float>(old);
    if(!std::isfinite(value) || value<0.f || value>1.f ||
       preferences[i]<-1 || preferences[i]>100)return {};
    if(preferences[i]<0)continue;
    const auto bits=std::bit_cast<uint32_t>(float(preferences[i])*0.01f);
    for(unsigned j=0;j<4;++j)plan.after[i*4+j]=uint8_t(bits>>(24-8*j));
  }
  return plan;
}
} // namespace hulk::eor::vm
