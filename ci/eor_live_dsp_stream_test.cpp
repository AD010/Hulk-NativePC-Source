#include "../native_port/src/eor_live_dsp_stream.h"
#include <cassert>
#include <thread>
using namespace hulk::eor::audio;
int main(){
  std::array<int16_t,8> pcm{16000,0,0,0,0,0,0,0},out{};
  StaticDspChain chain;chain.count=1;chain.stages[0]={DspKind::LowpassSimple,1000,1};
  LiveDspStream stream;assert(stream.Initialize(pcm,48000,1,0,chain));
  assert(stream.Fill(std::span<int16_t>(out).first(2))==2);
  chain.stages[0].value=2000;assert(stream.Publish(chain));
  assert(stream.Fill(std::span<int16_t>(out).subspan(2))==6&&stream.AtEnd());
  auto reference=pcm;StaticDspChain old=chain;old.stages[0].value=1000;
  const std::array<DspParameterBlock,2> blocks{{{0,old},{2,chain}}};
  assert(ApplyScheduledDsp(reference,48000,1,blocks)&&out==reference&&out[2]!=0);
  assert(stream.Fill(out)==0);
  LiveDspStream stereo;assert(stereo.Initialize(pcm,48000,2,1,chain));
  assert(stereo.Fill(std::span<int16_t>(out).first(3))==0);
  assert(stereo.Fill(out)==6&&stereo.AtEnd());
  chain.stages[0].value=-1;assert(!stereo.Publish(chain));
  LiveDspMailbox box;assert(box.Publish(old,48000));
  std::atomic<bool> done=false;
  std::thread writer([&]{for(unsigned i=0;i<20000;++i){auto c=old;c.stages[0].value=float(1000+i%2);c.stages[0].q=float(1+i%2);assert(box.Publish(c,48000));}done=true;});
  while(!done){StaticDspChain c;if(box.Read(c))assert(c.count==1&&c.stages[0].value-999.f==c.stages[0].q);}
  writer.join();
}
