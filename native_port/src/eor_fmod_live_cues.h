#pragma once
#include "eor_fmod_live_binding.h"
#include <map>
#include <tuple>
namespace hulk::eor::audio {
// Resolve the observed ordinal against the authenticated source definition.
// Reject mismatched layer, definition, weight, count or non-wave records.
inline std::optional<size_t> ResolveLiveFmodCue(const FevEventTable& table,
    const LiveFevBinding& binding,const LiveFmodCue& cue) noexcept {
  if(!cue.instance_owner||!cue.channel||binding.event_index>=table.events.size())return {};
  const auto& event=table.events[binding.event_index];
  if(cue.layer>=event.layers.size()||cue.instance>=event.layers[cue.layer].size())return {};
  const auto d=event.layers[cue.layer][cue.instance];
  if(d>=table.definitions.size())return {};
  const auto& def=table.definitions[d];
  if(cue.definition!=def.name||cue.variant_count!=def.variants.size()||
     def.weights.size()!=def.variants.size()||cue.variant>=def.variants.size()||
     cue.weight!=def.weights[cue.variant])return {};
  return def.variants[cue.variant];
}
struct LiveCueKey {
  uint32_t event,project,start,instance,channel,variant;
  auto Tie() const noexcept {return std::tie(event,project,start,instance,channel,variant);}
  bool operator<(const LiveCueKey& other) const noexcept {return Tie()<other.Tie();}
  bool operator==(const LiveCueKey& other) const noexcept {return Tie()==other.Tie();}
};
inline LiveCueKey KeyForLiveCue(const LiveFmodEvent& event,const LiveFmodCue& cue) noexcept {
  return {event.owner,event.project_owner,event.start_stamp,cue.instance_owner,cue.channel,cue.variant};
}
// Diagnostic shadow admission only. Remember failed/naturally ended voices
// until their observed instance changes; never restart on every update.
class LiveCueRegistry {
 public:
  template<class Start,class Stop>
  void Reconcile(std::span<const LiveFmodEvent> active,Start&& start,Stop&& stop,size_t budget=1) {
    for(auto it=voices_.begin();it!=voices_.end();){
      const auto& key=it->first;
      const auto event=std::find_if(active.begin(),active.end(),[&](const auto& e){
        return e.owner==key.event&&e.project_owner==key.project&&e.start_stamp==key.start;
      });
      const bool keep=event!=active.end()&&(!event->cues_complete||
          std::any_of(event->cues.begin(),event->cues.end(),[&](const auto& c){return KeyForLiveCue(*event,c)==key;}));
      if(keep){++it;continue;}
      if(it->second)stop(it->second);
      it=voices_.erase(it);
    }
    for(const auto& event:active){
      if(!event.cues_complete)continue;
      for(const auto& cue:event.cues){
        if(!budget||voices_.size()>=128)return;
        if(!cue.instance_owner||!cue.channel)continue;
        auto [it,inserted]=voices_.try_emplace(KeyForLiveCue(event,cue),0);
        if(!inserted)continue;
        --budget;it->second=start(event,cue);
      }
    }
  }
  void Clear() noexcept {voices_.clear();} // caller already stopped mixer
  size_t Size() const noexcept {return voices_.size();}
 private:
  std::map<LiveCueKey,uint64_t> voices_;
};
} // namespace hulk::eor::audio
