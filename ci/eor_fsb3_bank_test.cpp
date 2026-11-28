#include "../native_port/src/eor_fsb3_bank.h"
#include <cassert>
#include <string>
using namespace hulk::eor::audio;
static void Word(std::vector<std::byte>& b,uint32_t v,bool be=false) {
  for(unsigned i=0;i<4;++i)b.push_back(std::byte((v>>(8u*(be?3u-i:i)))&255u));
}
static void Text(std::vector<std::byte>& b,std::string_view s) {
  for(unsigned char c:s)b.push_back(std::byte(c));
}
int main() {
  const auto hash=AudioStemHash("demo_preloadsamples_0");assert(hash);
  for(bool be:{false,true}) {
    std::vector<std::byte> b;Text(b,"demo_preloadsamples_0.fsb");b.push_back(std::byte{});
    Word(b,*hash,be);Word(b,1,be);Text(b,"FSB3");
    for(auto w:{1u,88u,4u,0x30001u,0u})Word(b,w);
    const auto h=b.size();b.resize(h+88);b[h]=std::byte{80};b[h+2]=std::byte{'a'};
    b[h+32]=std::byte{1};b[h+36]=std::byte{4};b[h+52]=std::byte{0x80};
    b[h+53]=std::byte{0xbb};b[h+62]=std::byte{1};Word(b,0x12345678);
    const auto v=InspectOriginalWaveBank(b,*hash,be);assert(v && v->samples.size()==1 && v->samples[0].rate==48000 && v->samples[0].payload.size()==4);
    assert(!InspectOriginalWaveBank(b,*hash,!be));
    assert(!InspectOriginalWaveBank(b,*hash^1u,be));
    b.pop_back();assert(!InspectOriginalWaveBank(b,*hash,be));
  }
  std::vector<std::byte> fev;Text(fev,"FEV1");Word(fev,0x002c0000);Word(fev,5);Text(fev,"demo");fev.push_back(std::byte{});Word(fev,4);
  for(auto name:{"demo_streams_0","demo_preloadsamples_0","demo_preloadsamples_1","demo_preloadsamples_2"}) {
    Word(fev,512);Word(fev,32);Word(fev,uint32_t(std::string_view(name).size()+1));Text(fev,name);fev.push_back(std::byte{});
  }
  const auto refs=InspectOriginalFevBanks(fev,"demo");assert(refs && refs->size()==4 && (*refs)[1].hash==*hash);
  assert(!InspectOriginalFevBanks(fev,"wrong"));fev.pop_back();assert(!InspectOriginalFevBanks(fev,"demo"));
}
