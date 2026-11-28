#include "../native_port/src/eor_fev_waveform.h"
#include <cassert>
using namespace hulk::eor::audio;
static void Word(std::vector<std::byte>& b,uint32_t n) {
  for(unsigned i=0;i<4;++i)b.push_back(std::byte((n>>(8*i))&255u));
}
static void Text(std::vector<std::byte>& b,std::string_view s) {
  for(unsigned char c:s)b.push_back(std::byte(c));
}
static void String(std::vector<std::byte>& b,std::string_view s) {
  Word(b,uint32_t(s.size()+1));Text(b,s);b.push_back(std::byte{});
}
int main() {
  std::vector<std::byte> b;Text(b,"FEV1");Word(b,0x002c0000);String(b,"demo");Word(b,4);
  for(auto n:{"demo_streams_0","demo_preloadsamples_0","demo_preloadsamples_1","demo_preloadsamples_2"}) {
    Word(b,512);Word(b,32);String(b,n);
  }
  const auto offset=b.size();
  Word(b,0);Word(b,100);String(b,"../audio/example.wav");String(b,"demo_preloadsamples_1");
  Word(b,1);Word(b,123);
  const auto w=InspectFevWaveforms(b,"demo");
  assert(w&&w->size()==1&&w->front().offset==offset&&w->front().bank==2&&w->front().sample_ordinal==1&&w->front().duration_ms==123);
  auto trunc=b;trunc.pop_back();auto bad=InspectFevWaveforms(trunc,"demo");assert(bad&&bad->empty());
  b[offset+4]=std::byte{101};bad=InspectFevWaveforms(b,"demo");assert(bad&&bad->empty());
  OriginalWaveBank bank;bank.samples.push_back({"EXAMPLE.wav",1,0,1,1,{}});
  assert(ResolveFevWaveformSample(bank,w->front().path)==0u);
  bank.samples.push_back({"example.wav",1,0,1,1,{}});
  assert(!ResolveFevWaveformSample(bank,w->front().path));
  assert(!ResolveFevWaveformSample(bank,"missing.wav"));
  assert(ResolveFevWaveformSample(bank,w->front())==1u);
  auto wrong=w->front();wrong.sample_ordinal=2;
  assert(!ResolveFevWaveformSample(bank,wrong));
  wrong.sample_ordinal=1;wrong.path="other.wav";
  assert(!ResolveFevWaveformSample(bank,wrong));
}
