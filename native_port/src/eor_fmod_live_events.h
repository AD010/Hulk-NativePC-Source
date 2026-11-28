#pragma once
#include "eor_fmod_update_clock.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <vector>
namespace hulk::eor::audio {
// USA EventSystem project and active-event intrusive lists, not a playback takeover.
struct LiveFmodParameter {
  std::string name;
  float minimum=0,maximum=0,normalized=0,physical=0;
};
struct LiveFmodEvent {
  uint32_t owner=0,project_owner=0,flags=0;
  std::string project,group,name;
  std::vector<LiveFmodParameter> parameters;
};
template<class Read>
std::optional<std::vector<LiveFmodEvent>> CaptureLiveFmodEvents(uint32_t system,Read&& read) {
  constexpr uint32_t event_table=0x82089270u,group_table=0x820894a8u,
      project_table=0x82089848u,param_table=0x82089328u;
  const auto word=[&](uint32_t p,uint32_t off)->std::optional<uint32_t>{
    if(!p||(p&3u)||uint64_t(p)+off>UINT32_MAX-3u)return {};
    return ReadFmodBeWord(p+off,read);
  };
  const auto name=[&](uint32_t object,uint32_t off)->std::optional<std::string>{
    const auto address=word(object,off);
    if(!address||!*address||*address>UINT32_MAX-255u)return {};
    std::string s;
    for(uint32_t i=0;i<256;++i){
      std::byte c{};if(!read(*address+i,&c,1))return {};
      auto v=std::to_integer<unsigned char>(c);
      if(!v)return s.empty()?std::nullopt:std::optional<std::string>(std::move(s));
      if(v<32||v>=127)return {};
      s.push_back(static_cast<char>(v));
    }
    return {};
  };
  if(!CaptureFmodUpdateStamp(system,read)||system>UINT32_MAX-124u)return {};
  std::vector<LiveFmodEvent> result;
  const uint32_t project_end=system+124u;
  auto pn=word(system,124);unsigned projects=0;
  while(pn&&*pn!=project_end){
    if(*pn<4u||++projects>1024u)return {};
    const uint32_t project=*pn-4u;
    if(word(project,0)!=project_table||project>UINT32_MAX-64u)return {};
    const auto project_name=name(project,16);
    const uint32_t event_end=project+64u;
    auto en=word(project,64);
    while(en&&*en!=event_end){
      if(*en<44u||result.size()>=4096u)return {};
      const auto event=word(*en,8);
      if(!event||*event!=*en-44u||word(*event,0)!=event_table||*event>UINT32_MAX-344u)return {};
      if(std::any_of(result.begin(),result.end(),[&](const auto& e){return e.owner==*event;}))return {};
      const auto flags=word(*event,312),group=word(*event,68);
      if(!flags||!(*flags&0x1000u)||!group||word(*group,0)!=group_table||word(*group,44)!=project)return {};
      auto event_name=name(*event,84),group_name=name(*group,16);
      if(!project_name||!event_name||!group_name)return {};
      LiveFmodEvent out{*event,project,*flags,*project_name,*group_name,*event_name,{}};
      const uint32_t param_end=*event+344u;
      auto param_node=word(*event,344);
      while(param_node&&*param_node!=param_end){
        if(*param_node<4u||out.parameters.size()>=1024u)return {};
        const uint32_t param=*param_node-4u;
        if(word(param,0)!=param_table||word(param,44)!=*event)return {};
        const auto def=word(param,16),normalized=word(param,20);
        if(!def||!normalized)return {};
        const auto p_name=name(*def,4);const auto lo=word(*def,12),hi=word(*def,16);
        if(!p_name||!lo||!hi)return {};
        const float a=std::bit_cast<float>(*lo),b=std::bit_cast<float>(*hi),v=std::bit_cast<float>(*normalized);
        if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(b-a)||!(b>a)||!std::isfinite(v)||v<0.f||v>1.f)return {};
        out.parameters.push_back({*p_name,a,b,v,std::fma(b-a,v,a)});
        param_node=word(*param_node,0);
      }
      if(!param_node)return {};
      result.push_back(std::move(out));en=word(*en,0);
    }
    if(!en)return {};
    pn=word(*pn,0);
  }
  if(!pn)return {};
  return result;
}
} // namespace hulk::eor::audio
