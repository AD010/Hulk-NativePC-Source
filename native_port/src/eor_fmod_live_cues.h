#pragma once
#include "eor_fmod_live_binding.h"
#include <map>
#include <tuple>
#include <utility>
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
// Direct 2D shadow voices can use the source scalar gain in place of
// reconstructed FEV gain (never multiply the two). For 3D, the source
// scalar already includes distance-related factors; using it in the
// current spatial matrix would attenuate twice. Keep it diagnostic.
inline float ResolveLiveShadowGain(const LiveFmodCue& cue,float fev_gain) noexcept {
  if(cue.audibility&&!cue.spatial&&!cue.audibility->spatial&&
     std::isfinite(cue.audibility->combined)&&cue.audibility->combined>=0.f&&cue.audibility->combined<=1.f)
    return cue.audibility->combined;
  return fev_gain;
}
struct LiveCueKey {
  uint32_t event,project,start,instance,channel,variant;
  uint64_t serial=0;
  // The allocator may reuse the same channel/instance while selecting a
  // different layer or definition with the same variant ordinal. Own the
  // name: live event snapshots are replaced every audio update.
  uint32_t layer=0,instance_ordinal=0,variant_count=0,weight=0;
  std::string definition;
  auto Tie() const noexcept {return std::tie(event,project,start,instance,channel,variant,serial,
      layer,instance_ordinal,variant_count,weight,definition);}
  bool operator<(const LiveCueKey& other) const noexcept {return Tie()<other.Tie();}
  bool operator==(const LiveCueKey& other) const noexcept {return Tie()==other.Tie();}
};
inline bool MatchesLiveCue(const LiveCueKey& key,const LiveFmodEvent& event,const LiveFmodCue& cue) noexcept {
  return key.event==event.owner && key.project==event.project_owner &&
      key.start==event.start_stamp && key.instance==cue.instance_owner &&
      key.channel==cue.channel && key.variant==cue.variant && key.serial==cue.start_serial &&
      key.layer==cue.layer && key.instance_ordinal==cue.instance &&
      key.variant_count==cue.variant_count && key.weight==cue.weight &&
      key.definition==cue.definition;
}
inline LiveCueKey KeyForLiveCue(const LiveFmodEvent& event,const LiveFmodCue& cue) {
  return {event.owner,event.project_owner,event.start_stamp,cue.instance_owner,cue.channel,
      cue.variant,cue.start_serial,cue.layer,cue.instance,cue.variant_count,cue.weight,cue.definition};
}
// Diagnostic shadow admission only. Remember failed/naturally ended voices
// until their observed instance changes; never restart on every update.
class LiveCueRegistry {
 public:
  template<class Start,class Stop,class Ready>
  void ReconcileReady(std::span<const LiveFmodEvent> active,Start&& start,Stop&& stop,
                      size_t budget,Ready&& ready) {
    for(auto it=voices_.begin();it!=voices_.end();){
      const auto& key=it->first;
      const auto event=std::find_if(active.begin(),active.end(),[&](const auto& e){
        return e.owner==key.event&&e.project_owner==key.project&&e.start_stamp==key.start;
      });
      bool keep=false;
      if(event!=active.end()){
        const bool exact=std::any_of(event->cues.begin(),event->cues.end(),[&](const auto& c){return MatchesLiveCue(key,*event,c);});
        // A structurally valid partial snapshot can prove a channel ended,
        // even when unrelated channels have unknown waveform selections.
        const bool unknown=!event->cues_complete&&(!event->cue_channels_complete||
            std::any_of(event->unresolved_channels.begin(),event->unresolved_channels.end(),[&](const auto& c){
              return c.instance_owner==key.instance&&c.channel==key.channel;
            }));
        // A positive observation of a replacement on the SAME channel
        // overrides unrelated incomplete event coverage. Never retain the
        // old voice and start the replacement simultaneously.
        const bool replacement=!exact&&std::any_of(event->cues.begin(),event->cues.end(),
            [&](const auto& c){return c.instance_owner==key.instance&&c.channel==key.channel;});
        keep=exact||(!replacement&&unknown);
      }
      if(keep){++it;continue;}
      if(it->second)stop(it->second);
      it=voices_.erase(it);
    }
    for(const auto& event:active){
      // Partial snapshots may contain individually validated cues.
      for(const auto& cue:event.cues){
        if(!budget||voices_.size()>=128)return;
        if(!cue.instance_owner||!cue.channel||!ready(event,cue))continue;
        // An incomplete live pose/control snapshot is not a decoder failure.
        // Do not allocate a permanent tombstone or spend admission budget
        // until the original channel has sufficient current evidence.
        auto [it,inserted]=voices_.try_emplace(KeyForLiveCue(event,cue),0);
        if(!inserted)continue;
        --budget;it->second=start(event,cue);
      }
    }
  }
  template<class Start,class Stop>
  void Reconcile(std::span<const LiveFmodEvent> active,Start&& start,Stop&& stop,size_t budget=1) {
    ReconcileReady(active,std::forward<Start>(start),std::forward<Stop>(stop),budget,
        [](const LiveFmodEvent&,const LiveFmodCue&){return true;});
  }
  // Update only exact, currently observed identities. A failed update retires
  // that native voice but preserves the tombstone until the original cue ends.
  template<class Update,class Stop,class Ready>
  void UpdateActiveReady(std::span<const LiveFmodEvent> active,Update&& update,
                         Stop&& stop,Ready&& ready) {
    for(const auto& event:active){
      // Partial snapshots may contain individually validated cues.
      for(const auto& cue:event.cues){
        const auto it=voices_.find(KeyForLiveCue(event,cue));
        if(it==voices_.end()||!it->second)continue;
        // A temporarily incomplete pose is not a failed native setter.
        // Preserve the last verified controls and voice position until
        // a complete snapshot arrives, or reconciliation proves it ended.
        if(!ready(event,cue))continue;
        if(!update(it->second,event,cue)){stop(it->second);it->second=0;}
      }
    }
  }
  template<class Update,class Stop>
  void UpdateActive(std::span<const LiveFmodEvent> active,Update&& update,Stop&& stop) {
    UpdateActiveReady(active,std::forward<Update>(update),std::forward<Stop>(stop),
        [](const LiveFmodEvent&,const LiveFmodCue&){return true;});
  }
  void Clear() noexcept {voices_.clear();} // caller already stopped mixer
  size_t Size() const noexcept {return voices_.size();}
 private:
  std::map<LiveCueKey,uint64_t> voices_;
};
} // namespace hulk::eor::audio
