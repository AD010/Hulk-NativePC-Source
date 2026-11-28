#pragma once
#include "eor_fmod_live_events.h"
#include "eor_fev_automation.h"
namespace hulk::eor::audio {
struct LiveFevBinding {
  size_t event_index=0;
  std::vector<float> physical_values;
};
// Match the actual named guest instance to exactly one source event. Do not
// assume runtime parameter list order matches the serialized FEV order.
inline std::optional<LiveFevBinding> BindLiveFevEvent(const LiveFmodEvent& live,
    std::string_view project,const FevEventTable& table) {
  if(live.project!=project)return {};
  std::optional<size_t> match;
  for(size_t i=0;i<table.events.size();++i){
    const auto& e=table.events[i];
    if(e.group==live.group&&e.name==live.name){if(match)return {};match=i;}
  }
  if(!match)return {};
  const auto& event=table.events[*match];
  if(event.parameters.size()!=live.parameters.size())return {};
  LiveFevBinding out;out.event_index=*match;
  std::vector<bool> used(live.parameters.size());
  for(const auto& p:event.parameters){
    const auto domain=ReadFevParameterDomain(p);if(!domain)return {};
    std::optional<size_t> found;
    for(size_t i=0;i<live.parameters.size();++i){
      if(live.parameters[i].name==p.name){if(found)return {};found=i;}
    }
    if(!found||used[*found])return {};
    const auto& v=live.parameters[*found];
    if(v.minimum!=domain->minimum||v.maximum!=domain->maximum)return {};
    const auto physical=domain->Physical(v.normalized);
    if(!physical||*physical!=v.physical)return {};
    used[*found]=true;out.physical_values.push_back(*physical);
  }
  return out;
}
} // namespace hulk::eor::audio
