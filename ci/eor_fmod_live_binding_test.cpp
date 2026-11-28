#include "../native_port/src/eor_fmod_live_binding.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace hulk::eor::audio;
int main(int argc,char** argv){
  std::array<std::byte,24> fields{};fields[11]=std::byte{0x3f};fields[10]=std::byte{0x80};
  FevEventTable table;FevEventEntry e;e.group="preload";e.name="one";e.parameters={{"a",fields,{}},{"b",fields,{}}};table.events.push_back(e);
  LiveFmodEvent live;live.project="test";live.group="preload";live.name="one";
  live.parameters={{"b",0,1,.75f,.75f},{"a",0,1,.25f,.25f}};
  auto b=BindLiveFevEvent(live,"test",table);assert(b&&b->event_index==0&&b->physical_values==std::vector<float>({.25f,.75f}));
  assert(!BindLiveFevEvent(live,"wrong",table));
  live.parameters[0].maximum=2;assert(!BindLiveFevEvent(live,"test",table));live.parameters[0].maximum=1;
  live.parameters[0].name="a";assert(!BindLiveFevEvent(live,"test",table));live.parameters[0].name="b";
  table.events.push_back(e);assert(!BindLiveFevEvent(live,"test",table));
  if(argc==1)return 0;
  assert(argc==2);std::ifstream f(argv[1],std::ios::binary);assert(f);
  unsigned projects=0,events=0,matched=0,parameters=0;
  for(;;){uint32_t h[4];if(!f.read(reinterpret_cast<char*>(h),16)){assert(f.eof()&&!f.gcount());break;}
    std::vector<std::byte> bytes(h[3]);assert(f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));if(!h[0])continue;
    auto text=AudioText(bytes);auto end=text.find('\0');auto project=text.substr(0,end);
    auto parsed=InspectFevEventTable(AudioBytes(bytes).subspan(end+1),project);assert(parsed);++projects;
    for(size_t i=0;i<parsed->events.size();++i){
      const auto& event=parsed->events[i];++events;LiveFmodEvent sample;sample.project=project;sample.group=event.group;sample.name=event.name;
      for(const auto& p:event.parameters){const auto domain=ReadFevParameterDomain(p);assert(domain);sample.parameters.push_back({std::string(p.name),domain->minimum,domain->maximum,.25f,*domain->Physical(.25f)});++parameters;}
      std::reverse(sample.parameters.begin(),sample.parameters.end());
      auto result=BindLiveFevEvent(sample,project,*parsed);
      if(result){assert(result->event_index==i);assert(result->physical_values.size()==event.parameters.size());++matched;}
    }
  }
  std::cout<<projects<<" projects, "<<matched<<'/'<<events<<" unique event bindings, "<<parameters<<" parameter records\n";
}
