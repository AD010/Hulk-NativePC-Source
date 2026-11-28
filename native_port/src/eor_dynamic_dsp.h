#pragma once
#include "eor_static_dsp.h"
namespace hulk::eor::audio {
// Stateful physical-parameter processor. The caller must supply resolved
// parameters at audio-block boundaries; this does not infer FEV curve IDs.
class DynamicDspProcessor {
 public:
  bool Configure(uint32_t rate,uint16_t channels,const StaticDspChain& chain) noexcept {
    if((channels!=1&&channels!=2)||!ValidStaticDsp(chain,rate))return false;
    if(rate_!=rate||channels_!=channels){states_={};chain_={};}
    for(unsigned i=0;i<chain.count;++i){
      auto& state=states_[i];const auto& stage=chain.stages[i];
      const bool same=i<chain_.count&&chain_.stages[i].kind==stage.kind;
      if(!same)state={};
      if(!same||stage.value!=chain_.stages[i].value||stage.q!=chain_.stages[i].q){
        if(stage.kind==DspKind::Lowpass)for(auto& f:state.low)f.Configure(stage.value,rate,stage.q);
        if(stage.kind==DspKind::Highpass)for(auto& f:state.high)f.Configure(stage.value,rate,stage.q);
        if(stage.kind==DspKind::LowpassSimple)state.alpha=OriginalUsaSimpleLowpassAlpha(stage.value,rate);
      }
    }
    for(unsigned i=chain.count;i<states_.size();++i)states_[i]={};
    rate_=rate;channels_=channels;chain_=chain;return true;
  }
  bool Process(std::span<float> pcm) noexcept {
    if(!rate_||!channels_||pcm.size()%channels_)return false;
    for(float v:pcm)if(!std::isfinite(v))return false;
    for(size_t n=0;n<pcm.size();++n){
      const auto channel=n%channels_;float x=pcm[n];
      for(unsigned i=0;i<chain_.count;++i){
        auto& state=states_[i];const auto& stage=chain_.stages[i];
        switch(stage.kind){
          case DspKind::Distortion:x=OriginalUsaDistortion(x,stage.value);break;
          case DspKind::LowpassSimple:x=state.simple[channel].Process(x,state.alpha);break;
          case DspKind::Highpass:x=state.high[channel].Process(x);break;
          case DspKind::Lowpass:x=state.low[channel].Process(x);break;
          default:return false;
        }
      }
      pcm[n]=x;
    }
    return true;
  }
 private:
  struct StageState {
    std::array<OriginalUsaLowpass,2> low{};
    std::array<OriginalUsaHighpass,2> high{};
    std::array<OriginalUsaSimpleLowpassState,2> simple{};
    float alpha=0.f;
  };
  uint32_t rate_=0;uint16_t channels_=0;
  StaticDspChain chain_{};
  std::array<StageState,4> states_{};
};
struct DspParameterBlock {
  uint32_t first_frame=0;
  StaticDspChain chain{};
};
// One-shot pre-render of an explicit physical-parameter timeline. No FEV
// interpolation or runtime event parameter semantics are assumed here.
inline bool ApplyScheduledDsp(std::span<int16_t> pcm,uint32_t rate,uint16_t channels,
    std::span<const DspParameterBlock> blocks) noexcept {
  if((channels!=1&&channels!=2)||pcm.empty()||pcm.size()%channels||blocks.empty()||
      blocks.size()>16384||blocks.front().first_frame!=0)return false;
  const auto frames=pcm.size()/channels;
  for(size_t i=0;i<blocks.size();++i){
    if(blocks[i].first_frame>=frames||(i&&blocks[i].first_frame<=blocks[i-1].first_frame)||
        !ValidStaticDsp(blocks[i].chain,rate))return false;
  }
  DynamicDspProcessor processor;
  std::array<float,512> scratch{};
  for(size_t i=0;i<blocks.size();++i){
    if(!processor.Configure(rate,channels,blocks[i].chain))return false;
    size_t pos=size_t(blocks[i].first_frame)*channels;
    const size_t end=(i+1<blocks.size()?size_t(blocks[i+1].first_frame):frames)*channels;
    while(pos<end){
      const auto n=(std::min)(scratch.size(),end-pos);
      for(size_t j=0;j<n;++j)scratch[j]=pcm[pos+j]/32768.f;
      if(!processor.Process(std::span<float>(scratch).first(n)))return false;
      for(size_t j=0;j<n;++j){
        if(!std::isfinite(scratch[j]))return false;
        pcm[pos+j]=static_cast<int16_t>(std::lround(std::clamp(scratch[j]*32768.f,-32768.f,32767.f)));
      }
      pos+=n;
    }
  }
  return true;
}
} // namespace hulk::eor::audio
