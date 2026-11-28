#pragma once
#include "eor_fmod_start_trace.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <set>
#include <utility>
#include <vector>
namespace hulk::eor::audio {
// USA EventSystem project and active-event intrusive lists, not a playback takeover.
struct LiveFmodParameter {
  std::string name;
  float minimum=0,maximum=0,normalized=0,physical=0;
};
struct LiveFmodCue {
  uint32_t instance_owner=0,channel=0,layer=0,instance=0,variant=0,variant_count=0,weight=0;
  std::string definition;
  uint64_t start_serial=0;
};
struct LiveFmodChannelIdentity {
  uint32_t instance_owner=0,channel=0;
};
struct LiveFmodEvent {
  uint32_t owner=0,project_owner=0,flags=0;
  std::string project,group,name;
  std::vector<LiveFmodParameter> parameters;
  uint32_t parent_owner=0,start_stamp=0;
  std::vector<LiveFmodCue> cues;
  bool cues_complete=false;
  // Structural coverage is independent of whether every variant is known.
  bool cue_channels_complete=false;
  std::vector<LiveFmodChannelIdentity> unresolved_channels;
};
struct NoLiveFmodChildSelection {
  std::optional<LiveFmodSelectedOrdinal> operator()(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) const noexcept {return {};}
};
// A resolver may supply an independently captured start-time ordinal keyed by
// event, event start stamp, child, parent, channel and definition address.
// Never read the mutable parent ordinal to reconstruct an older child.
template<class Read,class Resolve=NoLiveFmodChildSelection>
std::optional<std::vector<LiveFmodEvent>> CaptureLiveFmodEvents(uint32_t system,Read&& read,Resolve&& resolve={}) {
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
  // USA 8283A3DC walks the sound instance's child list at +120.
  // 82838664..82838678 selects through the parent (+76); the index at
  // parent+48 is mutable. A child is unambiguous here only for a single
  // wave variant. A multi-variant child needs a start-time selection trace.
  struct CueSnapshot {std::vector<LiveFmodCue> items;std::vector<LiveFmodChannelIdentity> unresolved;bool complete=true;};
  const auto cues=[&](uint32_t event,uint32_t start_stamp)->std::optional<CueSnapshot>{
    CueSnapshot out;
    std::set<uint32_t> seen;
    const auto capture=[&](uint32_t node,uint32_t layer,uint32_t parent,uint32_t l,uint32_t i)->bool{
      if(!seen.insert(node).second||seen.size()>4096u||word(node,16)!=layer||word(node,76)!=parent)return false;
      const auto flags=word(node,20),channel=word(node,36);
      if(!flags||!channel)return false;
      if(!(*flags&8u)||!*channel)return true;
      const auto owner=word(node,12);if(!owner)return false;
      const auto def=word(*owner,4);if(!def)return false;
      if(parent){
        const auto po=word(parent,12);if(!po||word(*po,4)!=def)return false;
      }
      const auto count=word(*def,76),records=word(*def,84);
      const auto def_name=name(*def,12);
      if(!count||!*count||*count>4096u||!records||!*records||!def_name)return false;
      std::optional<uint32_t> variant;
      uint64_t serial=0;
      if(parent){
        if(const auto chosen=resolve(event,start_stamp,node,parent,*channel,*def)){
          variant=chosen->variant;serial=chosen->serial;
        }else if(*count==1u)variant=0u;
        else {out.complete=false;out.unresolved.push_back({node,*channel});return true;}
      }else variant=word(node,48);
      if(!variant||*variant>=*count||uint64_t(*records)+uint64_t(*variant)*24u>UINT32_MAX-7u)return false;
      const auto record=*records+*variant*24u;
      const auto type=word(record,0),weight=word(record,4);
      if(!type||!weight)return false;
      if(*type!=0){out.complete=false;return true;}
      out.items.push_back({node,*channel,l,i,*variant,*count,*weight,*def_name,serial});
      return true;
    };
    const auto layer_end=event+332u;
    auto layer=word(event,332);uint32_t l=0;
    while(layer&&*layer!=layer_end){
      if(l>=128||word(*layer,12)!=event||*layer>UINT32_MAX-84u)return {};
      const auto instance_end=*layer+84u;
      auto instance=word(*layer,84);uint32_t i=0;
      while(instance&&*instance!=instance_end){
        if(i>=4096||word(*instance,16)!=*layer||word(*instance,76)!=0u||*instance>UINT32_MAX-120u)return {};
        const auto child_end=*instance+120u;
        auto child=word(*instance,120);if(!child)return {};
        if(*child==child_end){
          if(!capture(*instance,*layer,0,l,i))return {};
        }else{
          // Parent's own channel may be a shared template; do not admit it.
          if(!seen.insert(*instance).second||seen.size()>4096u)return {};
          out.complete=false;
          // Do not admit the parent's possible template channel. Its lifetime
          // is ambiguous, so keep an existing matching voice conservatively.
          const auto flags=word(*instance,20),channel=word(*instance,36);
          if(!flags||!channel)return {};
          if((*flags&8u)&&*channel)out.unresolved.push_back({*instance,*channel});
          while(child&&*child!=child_end){
            if(!capture(*child,*layer,*instance,l,i))return {};
            child=word(*child,0);
          }
          if(!child)return {};
        }
        instance=word(*instance,0);++i;
      }
      if(!instance)return {};
      layer=word(*layer,0);++l;
    }
    if(!layer)return {};
    return out;
  };
  if(!CaptureFmodUpdateStamp(system,read)||system>UINT32_MAX-124u)return {};
  std::vector<LiveFmodEvent> result;
  std::set<uint32_t> visiting,visited;
  // 8283A718 recurses through event+20 (count), +24 (array), before
  // updating the parent. Inactive parents may still own active children.
  const auto visit=[&](auto&& self,uint32_t event,uint32_t parent,unsigned depth)->bool{
    if(depth>32||visiting.contains(event)||word(event,0)!=event_table||event>UINT32_MAX-344u)return false;
    if(visited.contains(event))return true;
    if(visited.size()+visiting.size()>=4096u)return false;
    visiting.insert(event);
    const auto count=word(event,20),children=word(event,24),flags=word(event,312);
    if(!count||*count>1024u||!children||!flags||(*count&&(!*children||uint64_t(*children)+4ull*(*count)>UINT32_MAX)))return false;
    for(uint32_t i=0;i<*count;++i){
      const auto child=word(*children,4u*i);
      if(!child||(*child&&!self(self,*child,event,depth+1)))return false;
    }
    if(*flags&0x1000u){
      const auto group=word(event,68),definition=word(event,32),start=word(event,264);
      if(!group||!definition||!start||word(*group,0)!=group_table)return false;
      const auto project=word(*group,44);
      if(!project||word(*project,0)!=project_table)return false;
      const uint32_t def=*definition?*definition:event;
      if(word(def,0)!=event_table)return false;
      const auto project_name=name(*project,16),event_name=name(def,84),group_name=name(*group,16);
      if(!project_name||!event_name||!group_name)return false;
      LiveFmodEvent out;
      out.owner=event;out.project_owner=*project;out.flags=*flags;out.project=*project_name;
      out.group=*group_name;out.name=*event_name;out.parent_owner=parent;out.start_stamp=*start;
      if(auto selected=cues(event,*start)){
        out.cues=std::move(selected->items);out.cues_complete=selected->complete;
        out.cue_channels_complete=true;out.unresolved_channels=std::move(selected->unresolved);
      }
      const uint32_t param_end=event+344u;
      auto param_node=word(event,344);
      while(param_node&&*param_node!=param_end){
        if(*param_node<4u||out.parameters.size()>=1024u)return false;
        const uint32_t param=*param_node-4u;
        if(word(param,0)!=param_table||word(param,44)!=event)return false;
        const auto def=word(param,16),normalized=word(param,20);
        if(!def||!normalized)return false;
        const auto p_name=name(*def,4);const auto lo=word(*def,12),hi=word(*def,16);
        if(!p_name||!lo||!hi)return false;
        const float a=std::bit_cast<float>(*lo),b=std::bit_cast<float>(*hi),v=std::bit_cast<float>(*normalized);
        if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(b-a)||!(b>a)||!std::isfinite(v)||v<0.f||v>1.f)return false;
        out.parameters.push_back({*p_name,a,b,v,std::fma(b-a,v,a)});
        param_node=word(*param_node,0);
      }
      if(!param_node)return false;
      result.push_back(std::move(out));
    }
    visiting.erase(event);visited.insert(event);return true;
  };
  const uint32_t project_end=system+124u;
  auto pn=word(system,124);unsigned projects=0;
  while(pn&&*pn!=project_end){
    if(*pn<4u||++projects>1024u)return {};
    const uint32_t project=*pn-4u;
    if(word(project,0)!=project_table||project>UINT32_MAX-64u)return {};
    const uint32_t event_end=project+64u;
    auto en=word(project,64);std::set<uint32_t> roots;
    while(en&&*en!=event_end){
      if(*en<44u||roots.size()>=4096u||!roots.insert(*en).second)return {};
      const auto event=word(*en,8);
      if(!event||*event!=*en-44u||!visit(visit,*event,0,0))return {};
      en=word(*en,0);
    }
    if(!en)return {};
    pn=word(*pn,0);
  }
  if(!pn)return {};
  return result;
}
} // namespace hulk::eor::audio
