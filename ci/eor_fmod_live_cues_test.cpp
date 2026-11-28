#include "../native_port/src/eor_fmod_live_cues.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace hulk::eor::audio;
int main(int argc,char** argv){
  FevEventTable table;table.events.resize(1);table.events[0].layers={{0}};
  FevSoundDefinition def;def.name="test";def.weights={50,25,25};def.variants={100,200,std::nullopt};table.definitions.push_back(def);
  LiveFevBinding binding;LiveFmodCue cue{1000,2000,0,0,1,3,25,"test"};
  assert(ResolveLiveFmodCue(table,binding,cue)==200u);
  auto bad=cue;bad.weight=50;assert(!ResolveLiveFmodCue(table,binding,bad));bad=cue;bad.variant=2;assert(!ResolveLiveFmodCue(table,binding,bad));
  bad=cue;bad.definition="wrong";assert(!ResolveLiveFmodCue(table,binding,bad));bad=cue;bad.layer=1;assert(!ResolveLiveFmodCue(table,binding,bad));
  LiveFmodEvent e;e.owner=100;e.project_owner=200;e.start_stamp=300;e.cues_complete=true;e.cues={cue};
  std::array<LiveFmodEvent,1> active{e};LiveCueRegistry registry;unsigned starts=0,stops=0;
  auto start=[&](const auto&,const auto&){return uint64_t(++starts);};auto stop=[&](uint64_t id){assert(id);++stops;};
  registry.Reconcile(active,start,stop);registry.Reconcile(active,start,stop);assert(starts==1&&stops==0);
  active[0].cues_complete=false;active[0].cues.clear();registry.Reconcile(active,start,stop);assert(stops==0);
  active[0]=e;active[0].cues[0].variant=0;registry.Reconcile(active,start,stop);assert(starts==2&&stops==1);
  active[0].start_stamp++;registry.Reconcile(active,start,stop);assert(starts==3&&stops==2);
  registry.Reconcile({},start,stop);assert(stops==3&&registry.Size()==0);
  auto fail=[&](const auto&,const auto&){++starts;return uint64_t(0);};
  registry.Reconcile(active,fail,stop);registry.Reconcile(active,fail,stop);assert(starts==4&&stops==3);
  registry.Reconcile({},start,stop);assert(stops==3);
  // Existing voices update without re-admission. Failed updates do not restart.
  active[0]=e;registry.Reconcile(active,start,stop);
  unsigned updates=0;auto update=[&](uint64_t,const auto&,const auto&){++updates;return true;};
  registry.UpdateActive(active,update,stop);assert(updates==1);
  registry.UpdateActive(active,[&](uint64_t,const auto&,const auto&){++updates;return false;},stop);
  assert(updates==2);const auto previous_starts=starts,previous_stops=stops;
  registry.Reconcile(active,start,stop);registry.UpdateActive(active,update,stop);
  assert(starts==previous_starts&&stops==previous_stops&&updates==2);
  registry.Reconcile({},start,stop);
  active[0]=e;active[0].cues_complete=false;
  active[0].cues.push_back(cue);active[0].cues.back().instance_owner++;
  registry.Reconcile(active,start,stop,2);assert(registry.Size()==2);
  const auto prior_updates=updates;registry.UpdateActive(active,update,stop);assert(updates==prior_updates+2);
  active[0].cues.clear();registry.Reconcile(active,start,stop);assert(registry.Size()==2);
  active[0].cues_complete=true;registry.Reconcile(active,start,stop);assert(registry.Size()==0);
  active[0]=e;active[0].cues_complete=false;active[0].cue_channels_complete=true;
  auto other=cue;other.instance_owner++;other.channel++;
  active[0].cues.push_back(other);registry.Reconcile(active,start,stop,2);assert(registry.Size()==2);
  active[0].cues.clear();active[0].unresolved_channels={{other.instance_owner,other.channel}};
  registry.Reconcile(active,start,stop);assert(registry.Size()==1);
  // Reusing the same channel with a known different variant replaces it.
  active[0].unresolved_channels.clear();other.variant=0;active[0].cues={other};
  const auto before_reuse=stops;registry.Reconcile(active,start,stop);
  assert(registry.Size()==1&&stops==before_reuse+1);
  active[0].cues.clear();registry.Reconcile(active,start,stop);assert(registry.Size()==0);
  active[0]=e;active[0].cues[0].start_serial=1;registry.Reconcile(active,start,stop);
  const auto before_restart=stops;active[0].cues[0].start_serial=2;registry.Reconcile(active,start,stop);
  assert(registry.Size()==1&&stops==before_restart+1);registry.Reconcile({},start,stop);
  FevEventEntry controlled;controlled.layer_records.resize(1);
  std::array<std::byte,24> fields{};fields[10]=std::byte{0x80};fields[11]=std::byte{0x3f};
  controlled.parameters.push_back({"Intensity",fields,{}});
  FevEffectRecord fx;fx.control=UINT32_MAX;fx.header={0,0x0c};fx.points={{{0,0,2}},{{0x3f800000,0x3f800000,2}}};
  controlled.layer_records[0].effects.push_back(fx);
  std::array<float,1> values{.25f};auto controls=ResolveLiveMutableControls(controlled,0,values);
  assert(controls&&controls->gain==.25f);values[0]=.75f;
  assert(ResolveLiveMutableControls(controlled,0,values)->gain==.75f);
  controlled.layer_records[0].effects[0].header[1]=0x404;
  assert(!ResolveLiveMutableControls(controlled,0,values));
  controlled.layer_records[0].effects[0].header[1]=0x44;
  assert(!ResolveLiveMutableControls(controlled,0,values));
  if(argc==1)return 0;
  assert(argc==2);std::ifstream f(argv[1],std::ios::binary);assert(f);unsigned projects=0;size_t waves=0,nonwaves=0;
  for(;;){uint32_t h[4];if(!f.read(reinterpret_cast<char*>(h),16)){assert(f.eof()&&!f.gcount());break;}
    std::vector<std::byte> bytes(h[3]);assert(f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));if(!h[0])continue;
    auto text=AudioText(bytes);auto end=text.find('\0');auto parsed=InspectFevEventTable(AudioBytes(bytes).subspan(end+1),text.substr(0,end));assert(parsed);++projects;
    for(size_t n=0;n<parsed->events.size();++n){
      const auto& event=parsed->events[n];LiveFevBinding b;b.event_index=n;
      for(uint32_t l=0;l<event.layers.size();++l)for(uint32_t i=0;i<event.layers[l].size();++i){
        const auto& d=parsed->definitions[event.layers[l][i]];
        for(uint32_t v=0;v<d.variants.size();++v){
          LiveFmodCue c{1,2,l,i,v,static_cast<uint32_t>(d.variants.size()),d.weights[v],std::string(d.name)};
          assert(ResolveLiveFmodCue(*parsed,b,c)==d.variants[v]);
          if(d.variants[v])++waves;else ++nonwaves;
        }
      }
    }
  }
  std::cout<<projects<<" projects, "<<waves<<" source waveform routes, "<<nonwaves<<" non-wave selections rejected\n";
}
