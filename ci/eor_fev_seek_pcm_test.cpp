#include "../native_port/src/eor_fev_seek_pcm.h"
#include <cassert>
using namespace hulk::eor::audio;
int main(){
  std::vector<int16_t> source;
  for(int16_t i=0;i<16;++i){source.push_back(i);source.push_back(-i);}
  std::array<FevVoiceBlock,3> blocks{};
  blocks[0].controls.offset_ms=0;
  blocks[1].first_frame=4;blocks[1].controls.offset_ms=2;
  blocks[2].first_frame=6;blocks[2].controls.offset_ms=0;
  const auto rendered=RenderFevSeekPcm(source,4000,2,blocks,100);
  assert(rendered&&rendered->size()==44);
  for(size_t i=0;i<22;++i){
    const int16_t expected=static_cast<int16_t>(i<4?i:i<6?i+4:i-6);
    assert((*rendered)[2*i]==expected&&(*rendered)[2*i+1]==-expected);
  }
  assert(!RenderFevSeekPcm(source,4000,2,blocks,43));
  auto bad=blocks;bad[2].controls.offset_ms.reset();assert(!RenderFevSeekPcm(source,4000,2,bad,100));
  bad=blocks;bad[1].first_frame=0;assert(!RenderFevSeekPcm(source,4000,2,bad,100));
  auto end=blocks;end[0].controls.offset_ms=3;end[1].first_frame=5;
  const auto short_pcm=RenderFevSeekPcm(source,4000,2,end,100);
  assert(short_pcm&&short_pcm->size()==8&&(*short_pcm)[0]==12);
  // DSP sees a continuous sequence in event order, including backward seeks.
  auto processed=*rendered;
  StaticDspChain chain;chain.count=1;chain.stages[0]={DspKind::LowpassSimple,1000,1};
  std::array<DspParameterBlock,2> dsp{{{0,chain},{6,chain}}};
  auto reference=processed;assert(ApplyScheduledDsp(processed,4000,2,dsp));
  assert(ApplyScheduledDsp(reference,4000,2,std::span<const DspParameterBlock>(dsp).first(1)));
  assert(processed==reference);
}
