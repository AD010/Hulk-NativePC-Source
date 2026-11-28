#pragma once
#include "eor_fmod_live_binding.h"
#include <map>
#include <tuple>
#include "eor_fev_voice_schedule.h"
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
// Only controls with a native live setter. Named DSP is currently baked into
// PCM, so it cannot be updated by this path without changing filter history.
inline std::optional<FevGainPitch> ResolveLiveMutableControls(const FevEventEntry& event,
    uint32_t layer,std::span<const float> parameters,bool spatial_ready=false) noexcept {
  if(layer>=event.layer_records.size())return {};
  for(const auto& effect:event.layer_records[layer].effects){
    const auto route=ReadFevCurveRoute(event,effect);
    if(!route||(route->target!=FevTarget::Volume&&route->target!=FevTarget::Pitch&&route->target!=FevTarget::Pan&&
       !(spatial_ready&&route->target==FevTarget::Pan3DLevel)))return {};
  }
  const auto controls=EvaluateFevLayerGainPitch(event,layer,parameters,spatial_ready);
  if(!controls||controls->unhandled||!ValidFevVoiceRatio(controls->values.ratio)||
     !std::isfinite(controls->values.gain)||controls->values.gain<0.f||controls->values.gain>1.f)return {};
  return controls->values;
}
struct LiveCueKey {
  uint32_t event,project,start,instance,channel,variant;
  uint64_t serial=0;
  auto Tie() const noexcept {return std::tie(event,project,start,instance,channel,variant,serial);}
  bool operator<(const LiveCueKey& other) const noexcept {return Tie()<other.Tie();}
  bool operator==(const LiveCueKey& other) const noexcept {return Tie()==other.Tie();}
};
inline LiveCueKey KeyForLiveCue(const LiveFmodEvent& event,const LiveFmodCue& cue) noexcept {
  return {event.owner,event.project_owner,event.start_stamp,cue.instance_owner,cue.channel,cue.variant,cue.start_serial};
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
      bool keep=false;
      if(event!=active.end()){
        const bool exact=std::any_of(event->cues.begin(),event->cues.end(),[&](const auto& c){return KeyForLiveCue(*event,c)==key;});
        // A structurally valid partial snapshot can prove a channel ended,
        // even when unrelated channels have unknown waveform selections.
        const bool unknown=!event->cues_complete&&(!event->cue_channels_complete||
            std::any_of(event->unresolved_channels.begin(),event->unresolved_channels.end(),[&](const auto& c){
              return c.instance_owner==key.instance&&c.channel==key.channel;
            }));
        keep=exact||unknown;
      }
      if(keep){++it;continue;}
      if(it->second)stop(it->second);
      it=voices_.erase(it);
    }
    for(const auto& event:active){
      // Partial snapshots may contain individually validated cues.
      for(const auto& cue:event.cues){
        if(!budget||voices_.size()>=128)return;
        if(!cue.instance_owner||!cue.channel)continue;
        auto [it,inserted]=voices_.try_emplace(KeyForLiveCue(event,cue),0);
        if(!inserted)continue;
        --budget;it->second=start(event,cue);
      }
    }
  }
  // Update only exact, currently observed identities. A failed update retires
  // that native voice but preserves the tombstone until the original cue ends.
  template<class Update,class Stop>
  void UpdateActive(std::span<const LiveFmodEvent> active,Update&& update,Stop&& stop) {
    for(const auto& event:active){
      // Partial snapshots may contain individually validated cues.
      for(const auto& cue:event.cues){
        const auto it=voices_.find(KeyForLiveCue(event,cue));
        if(it==voices_.end()||!it->second)continue;
        if(!update(it->second,event,cue)){stop(it->second);it->second=0;}
      }
    }
  }
  void Clear() noexcept {voices_.clear();} // caller already stopped mixer
  size_t Size() const noexcept {return voices_.size();}
 private:
  std::map<LiveCueKey,uint64_t> voices_;
};
} // namespace hulk::eor::audio
