#pragma once
#include "eor_fev_automation.h"
namespace hulk::eor::audio {
// Original single pitch envelope exp2(8*v-4) spans four octaves in
// either direction. Keep a bounded native range, but do not discard
// legitimate values outside the previous quarter-to-fourfold limit.
inline constexpr float kMinFevVoiceRatio=.0625f,kMaxFevVoiceRatio=16.f;
inline bool ValidFevVoiceRatio(float ratio) noexcept {
  return std::isfinite(ratio)&&ratio>=kMinFevVoiceRatio&&ratio<=kMaxFevVoiceRatio;
}
struct FevVoiceBlock {
  uint32_t first_frame=0;
  FevGainPitch controls;
};
// Explicit source-frame schedule. A moving seek needs a separate owner
// operation; a 3D curve needs actual listener/emitter coordinates.
inline std::optional<std::vector<FevVoiceBlock>> BuildFevVoiceSchedule(
    const FevEventEntry& event,uint32_t layer,std::span<const FevParameterSnapshot> snapshots,bool spatial_ready=false) {
  if(snapshots.empty()||snapshots.size()>16384||snapshots.front().first_frame!=0)return {};
  std::vector<FevVoiceBlock> result;result.reserve(snapshots.size());
  for(const auto& snapshot:snapshots){
    if(!result.empty()&&snapshot.first_frame<=result.back().first_frame)return {};
    const auto value=EvaluateFevLayerGainPitch(event,layer,snapshot.physical_values,spatial_ready);
    if(!value||value->unhandled||(!spatial_ready&&value->values.pan3d_level))return {};
    if(!result.empty()&&(value->values.offset_ms!=result.front().controls.offset_ms||
        value->values.pan.has_value()!=result.front().controls.pan.has_value()))return {};
    result.push_back({snapshot.first_frame,value->values});
  }
  return result;
}
inline bool ValidFevVoiceSchedule(std::span<const FevVoiceBlock> blocks,uint32_t frames,float base_ratio,bool spatial_ready=false) noexcept {
  if(blocks.empty())return true;
  if(blocks.size()>16384||blocks.front().first_frame!=0)return false;
  for(size_t i=0;i<blocks.size();++i){
    const auto& b=blocks[i];const auto& c=b.controls;
    const float ratio=base_ratio*c.ratio;
    if(b.first_frame>=frames||(i&&b.first_frame<=blocks[i-1].first_frame)||
       !std::isfinite(c.gain)||c.gain<0.f||c.gain>1.f||
       !ValidFevVoiceRatio(ratio)||
       (c.pan3d_level&&(!spatial_ready||!std::isfinite(*c.pan3d_level)||*c.pan3d_level<0.f||*c.pan3d_level>1.f))||
       c.offset_ms!=blocks.front().controls.offset_ms||
       c.pan.has_value()!=blocks.front().controls.pan.has_value()||
       (c.pan&&(!std::isfinite(*c.pan)||*c.pan< -1.f||*c.pan>1.f)))return false;
  }
  return true;
}
inline std::optional<float> RequiredFevVoiceRatio(std::span<const FevVoiceBlock> blocks,float base) noexcept {
  if(!ValidFevVoiceRatio(base))return {};
  float result=(std::max)(4.f,base);
  for(const auto& b:blocks){
    const float ratio=base*b.controls.ratio;
    if(!ValidFevVoiceRatio(ratio))return {};
    result=(std::max)(result,ratio);
  }
  return result;
}
inline size_t FevVoiceBlockAt(std::span<const FevVoiceBlock> blocks,uint64_t frame) noexcept {
  if(blocks.empty())return 0;
  const auto it=std::upper_bound(blocks.begin(),blocks.end(),frame,
      [](uint64_t f,const FevVoiceBlock& b){return f<b.first_frame;});
  return it==blocks.begin()?0:static_cast<size_t>(it-blocks.begin()-1);
}
} // namespace hulk::eor::audio
