#pragma once
#include "eor_fev_automation.h"
namespace hulk::eor::audio {
struct FevVoiceBlock {
  uint32_t first_frame=0;
  FevGainPitch controls;
};
// Explicit source-frame schedule. A moving seek needs a separate owner
// operation; a 3D curve needs actual listener/emitter coordinates.
inline std::optional<std::vector<FevVoiceBlock>> BuildFevVoiceSchedule(
    const FevEventEntry& event,uint32_t layer,std::span<const FevParameterSnapshot> snapshots) {
  if(snapshots.empty()||snapshots.size()>16384||snapshots.front().first_frame!=0)return {};
  std::vector<FevVoiceBlock> result;result.reserve(snapshots.size());
  for(const auto& snapshot:snapshots){
    if(!result.empty()&&snapshot.first_frame<=result.back().first_frame)return {};
    const auto value=EvaluateFevLayerGainPitch(event,layer,snapshot.physical_values);
    if(!value||value->unhandled||value->values.pan3d_level)return {};
    if(!result.empty()&&(value->values.offset_ms!=result.front().controls.offset_ms||
        value->values.pan.has_value()!=result.front().controls.pan.has_value()))return {};
    result.push_back({snapshot.first_frame,value->values});
  }
  return result;
}
inline bool ValidFevVoiceSchedule(std::span<const FevVoiceBlock> blocks,uint32_t frames,float base_ratio) noexcept {
  if(blocks.empty())return true;
  if(blocks.size()>16384||blocks.front().first_frame!=0)return false;
  for(size_t i=0;i<blocks.size();++i){
    const auto& b=blocks[i];const auto& c=b.controls;
    const float ratio=base_ratio*c.ratio;
    if(b.first_frame>=frames||(i&&b.first_frame<=blocks[i-1].first_frame)||
       !std::isfinite(c.gain)||c.gain<0.f||c.gain>1.f||
       !std::isfinite(ratio)||ratio<.25f||ratio>4.f||c.pan3d_level||
       c.offset_ms!=blocks.front().controls.offset_ms||
       c.pan.has_value()!=blocks.front().controls.pan.has_value()||
       (c.pan&&(!std::isfinite(*c.pan)||*c.pan< -1.f||*c.pan>1.f)))return false;
  }
  return true;
}
inline size_t FevVoiceBlockAt(std::span<const FevVoiceBlock> blocks,uint64_t frame) noexcept {
  if(blocks.empty())return 0;
  const auto it=std::upper_bound(blocks.begin(),blocks.end(),frame,
      [](uint64_t f,const FevVoiceBlock& b){return f<b.first_frame;});
  return it==blocks.begin()?0:static_cast<size_t>(it-blocks.begin()-1);
}
} // namespace hulk::eor::audio
