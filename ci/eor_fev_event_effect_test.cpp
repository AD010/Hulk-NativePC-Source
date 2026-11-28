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
  U(b,1);S(b,"Intensity");U(b,0x3f800000);Z(b,20);U(b,2);U(b,4);U(b,7);
  U(b,3);S(b,"flag");U(b,0);U(b,123);S(b,"level");U(b,1);U(b,0x3f000000);
  S(b,"label");U(b,2);S(b,"test");U(b,1);S(b,"sfx");
  S(b,"nonpreload");U(b,0);U(b,0);U(b,0);
  U(b,1);S(b,"sound");Z(b,60);U(b,2);
  const auto at=b.size();U(b,0);U(b,100);S(b,"sound.wav");S(b,"demo_preloadsamples_0");U(b,0);U(b,50);
  U(b,2);U(b,100);U(b,0);
  const auto table=InspectFevEventTable(b,"demo");
  assert(table&&table->events.size()==1&&table->definitions.size()==1&&table->events[0].has_effects);
  assert(table->events[0].layers[0][0]==0);
  const auto& event=table->events[0];
  assert(event.fields.size()==124 && event.layer_records.size()==1);
  const auto& layer=event.layer_records[0];
  assert(layer.header.size()==6 && layer.instance_fields.size()==1);
  assert(layer.instance_fields[0].size()==64 && layer.effects.size()==2);
  assert(layer.effects[0].name=="FMOD Lowpass Simple");
  assert(layer.effects[1].name=="FMOD Distortion");
  assert(layer.effects[0].control==UINT32_MAX && layer.effects[0].points.size()==3);
  assert(layer.effects[0].points[0][1]==0x3f800000u);
  assert(layer.effects[1].tail[1]==1);
  assert(table->definitions[0].fields.size()==60 && table->definitions[0].weights.size()==2);
  assert(table->definitions[0].weights[0]==100);
  assert(event.has_parameters && event.parameters.size()==1);
  const auto& parameter=event.parameters[0];
  assert(parameter.name=="Intensity" && parameter.fields.size()==24);
  assert(AudioU32(parameter.fields,0)==0x3f800000u);
  assert(parameter.values.size()==2 && parameter.values[0]==4 && parameter.values[1]==7);
  assert(event.has_properties && event.properties.size()==3 && event.category_word==1);
  assert(event.properties[0].name=="flag" && event.properties[0].type==0 && event.properties[0].word==123);
  assert(event.properties[1].type==1 && event.properties[1].word==0x3f000000u);
  assert(event.properties[2].type==2 && event.properties[2].text=="test");
  assert(table->definitions[0].variants[0]==at&&!table->definitions[0].variants[1]);
  auto wrong=b;wrong[point_count+3]=std::byte{1};assert(!InspectFevEventTable(wrong,"demo"));
  for(size_t i=0;i<b.size();++i)assert(!InspectFevEventTable(AudioBytes(b).first(i),"demo"));
}
