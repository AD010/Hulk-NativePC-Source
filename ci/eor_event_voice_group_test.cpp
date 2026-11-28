#include "../native_port/src/eor_event_voice_group.h"
#include <cassert>
#include <array>
#include <set>
using namespace hulk::eor::audio;
int main(){
  const std::array<int,3> cues{1,2,3};
  for(int failure=0;failure<=6;++failure){
    std::set<uint64_t> held,playing;
    unsigned admitted=0,resumed=0;
    auto r=AdmitEventVoiceGroup<int>(cues,[&](int cue)->uint64_t{
      assert(playing.empty());++admitted;
      if(failure==cue)return 0;
      held.insert(cue);return cue;
    },[&](int cue,uint64_t id){
      assert(admitted==3);++resumed;
      if(failure==cue+3)return false;
      assert(held.contains(id));playing.insert(id);return true;
    },[&](uint64_t id) noexcept {held.erase(id);playing.erase(id);});
    if(failure){assert(!r&&held.empty()&&playing.empty());}
    else {assert(r&&r->size()==3&&playing.size()==3&&resumed==3);}
  }
  auto r=AdmitEventVoiceGroup<int>({},[](int){return 1u;},[](int,uint64_t){return true;},[](uint64_t) noexcept{});assert(!r);
}
