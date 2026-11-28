#pragma once
#include "eor_fmod_update_clock.h"
#include <map>
#include <limits>
namespace hulk::eor::audio {
struct LiveFmodSelectedOrdinal {uint32_t variant=0;uint64_t serial=0;};
struct FmodStartIdentity {
  uint32_t event=0,stamp=0,child=0,parent=0,definition=0;
  bool operator==(const FmodStartIdentity&) const noexcept = default;
};
struct FmodPendingSelection {FmodStartIdentity key;uint32_t variant=0;};
// Scope is emitted only in the source-verified start function. A selection
// from a failed/returned or unrelated start cannot leak into a later start.
class FmodStartTraceScope {
 public:
  explicit FmodStartTraceScope(uint8_t* base) noexcept:previous_(current_),base_(base){current_=this;}
  ~FmodStartTraceScope() noexcept {current_=previous_;}
  FmodStartTraceScope(const FmodStartTraceScope&)=delete;
  FmodStartTraceScope& operator=(const FmodStartTraceScope&)=delete;
  static bool Active(uint8_t* base) noexcept {return current_&&base&&current_->base_==base;}
  static void Reset(uint8_t* base) noexcept {if(Active(base))current_->pending_.reset();}
  static void Record(uint8_t* base,FmodPendingSelection value) noexcept {
    if(Active(base))current_->pending_=value;
  }
  static std::optional<FmodPendingSelection> Consume(uint8_t* base) noexcept {
    if(!Active(base))return {};
    auto value=current_->pending_;current_->pending_.reset();return value;
  }
 private:
  inline static thread_local FmodStartTraceScope* current_=nullptr;
  FmodStartTraceScope* previous_;
  uint8_t* base_;
  std::optional<FmodPendingSelection> pending_;
};
template<class Read>
std::optional<uint32_t> FmodTraceWord(uint32_t address,uint32_t offset,Read&& read) noexcept {
  if(!address||(address&3u)||uint64_t(address)+offset>UINT32_MAX-3u)return {};
  return ReadFmodBeWord(address+offset,read);
}
template<class Read>
std::optional<FmodStartIdentity> ReadFmodStartIdentity(uint32_t child,Read&& read) noexcept {
  const auto word=[&](uint32_t p,uint32_t o){return FmodTraceWord(p,o,read);};
  const auto parent=word(child,76),layer=word(child,16),owner=word(child,12);
  if(!parent||!*parent||*parent==child||!layer||!owner||word(*parent,76)!=0u||word(*parent,16)!=layer)return {};
  const auto parent_owner=word(*parent,12),def=word(*owner,4),event=word(*layer,12);
  if(!parent_owner||!def||!*def||word(*parent_owner,4)!=def||!event||word(*event,0)!=0x82089270u)return {};
  const auto stamp=word(*event,264);if(!stamp)return {};
  return FmodStartIdentity{*event,*stamp,child,*parent,*def};
}
template<class Read>
std::optional<FmodPendingSelection> CaptureFmodSelectionStore(uint32_t child,uint32_t parent,uint32_t ordinal,Read&& read) noexcept {
  const auto key=ReadFmodStartIdentity(child,read);
  if(!key||key->parent!=parent||FmodTraceWord(parent,48,read)!=ordinal)return {};
  const auto count=FmodTraceWord(key->definition,76,read);
  if(!count||!*count||*count>4096u||ordinal>=*count)return {};
  return FmodPendingSelection{*key,ordinal};
}
// USA 828387A0 reads the cached ordinal; 828387BC indexes 24-byte
// records, 828387D8 tests the record kind. Only original wave records
// (kind zero) qualify. The successful channel hook still confirms identity.
template<class Read>
std::optional<FmodPendingSelection> CaptureFmodCachedRecord(uint32_t child,uint32_t ordinal,Read&& read) noexcept {
  const auto key=ReadFmodStartIdentity(child,read);
  if(!key||FmodTraceWord(key->parent,44,read)!=ordinal)return {};
  const auto count=FmodTraceWord(key->definition,76,read),table=FmodTraceWord(key->definition,84,read);
  if(!count||!*count||*count>4096u||ordinal>=*count||!table||!*table||
      FmodTraceWord(*table,ordinal*24u,read)!=0u)return {};
  return FmodPendingSelection{*key,ordinal};
}
template<class Read>
std::optional<uint32_t> ConfirmFmodChannelStart(const FmodPendingSelection& selection,uint32_t child,Read&& read) noexcept {
  if(ReadFmodStartIdentity(child,read)!=selection.key)return {};
  const auto flags=FmodTraceWord(child,20,read),channel=FmodTraceWord(child,36,read);
  if(!flags||!(*flags&8u)||!channel||!*channel)return {};
  return channel;
}
// External lock protects this bounded registry. Serial distinguishes two
// successful starts that reuse the same guest instance, channel and variant.
class FmodStartTraceRegistry {
 public:
  void Forget(uint32_t child) noexcept {entries_.erase(child);}
  bool Observe(const FmodPendingSelection& selected,uint32_t channel) {
    const auto child=selected.key.child;Forget(child);
    if(!child||!channel||entries_.size()>=4096u||serial_==std::numeric_limits<uint64_t>::max())return false;
    entries_.emplace(child,Entry{selected,channel,++serial_});return true;
  }
  std::optional<LiveFmodSelectedOrdinal> Resolve(uint32_t event,uint32_t stamp,uint32_t child,uint32_t parent,
      uint32_t channel,uint32_t definition) const noexcept {
    const auto it=entries_.find(child);if(it==entries_.end())return {};
    const auto& e=it->second;
    if(e.channel!=channel||e.selected.key!=FmodStartIdentity{event,stamp,child,parent,definition})return {};
    return LiveFmodSelectedOrdinal{e.selected.variant,e.serial};
  }
  template<class Keep> void Retain(Keep&& keep) {
    for(auto it=entries_.begin();it!=entries_.end();){
      if(keep(it->second.selected.key,it->second.channel))++it;else it=entries_.erase(it);
    }
  }
  void Clear() noexcept {entries_.clear();} // keep monotonic serial
 private:
  struct Entry {FmodPendingSelection selected;uint32_t channel;uint64_t serial;};
  std::map<uint32_t,Entry> entries_;
  uint64_t serial_=0;
};
} // namespace hulk::eor::audio
