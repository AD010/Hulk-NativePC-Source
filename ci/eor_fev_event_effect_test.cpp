#include "../native_port/src/eor_fev_event_table.h"
#include <cassert>
using namespace hulk::eor::audio;
static void U(std::vector<std::byte>& b,uint32_t n) {for(unsigned i=0;i<4;++i)b.push_back(std::byte((n>>(8*i))&255u));}
static void S(std::vector<std::byte>& b,std::string_view s) {U(b,uint32_t(s.size()+1));for(unsigned char c:s)b.push_back(std::byte(c));b.push_back(std::byte{});}
static void Z(std::vector<std::byte>& b,size_t n){b.resize(b.size()+n);}
int main(){
  std::vector<std::byte> b;U(b,0x31564546);U(b,0x002c0000);S(b,"demo");U(b,4);
  for(auto n:{"demo_streams_0","demo_preloadsamples_0","demo_preloadsamples_1","demo_preloadsamples_2"}) {U(b,512);U(b,32);S(b,n);}
  S(b,"master");Z(b,20);U(b,2);
  S(b,"preload");U(b,0);U(b,0);U(b,1);
  S(b,"event");Z(b,124);U(b,1);Z(b,6);U(b,0x00020001u);Z(b,66);
  size_t point_count=0;
  for(auto effect:{"FMOD Lowpass Simple","FMOD Distortion"}){
    U(b,UINT32_MAX);S(b,effect);U(b,0);U(b,4);point_count=b.size();U(b,3);
    for(unsigned i=0;i<3;++i){U(b,0x3f000000);U(b,0x3f800000);U(b,1);}
    U(b,0);U(b,1);
  }
  U(b,0);U(b,0);U(b,1);S(b,"sfx");
  S(b,"nonpreload");U(b,0);U(b,0);U(b,0);
  U(b,1);S(b,"sound");Z(b,60);U(b,2);
  const auto at=b.size();U(b,0);U(b,100);S(b,"sound.wav");S(b,"demo_preloadsamples_0");U(b,0);U(b,50);
  U(b,2);U(b,100);U(b,0);
  const auto table=InspectFevEventTable(b,"demo");
  assert(table&&table->events.size()==1&&table->definitions.size()==1&&table->events[0].has_effects);
  assert(table->events[0].layers[0][0]==0);
  assert(table->definitions[0].variants[0]==at&&!table->definitions[0].variants[1]);
  auto wrong=b;wrong[point_count+3]=std::byte{1};assert(!InspectFevEventTable(wrong,"demo"));
  for(size_t i=0;i<b.size();++i)assert(!InspectFevEventTable(AudioBytes(b).first(i),"demo"));
}
