#pragma once
#include "eor_fev_waveform.h"
#include <stdexcept>
namespace hulk::eor::audio {
struct FevEventEntry {
  std::string_view group,name,category;
  std::vector<std::vector<uint16_t>> layers;
  bool has_effects=false,has_parameters=false,has_properties=false;
};
struct FevSoundDefinition {
  std::string_view name;
  // nullopt is a type-2 record, not an audio sample.
  std::vector<std::optional<size_t>> variants;
};
struct FevEventTable {
  std::vector<FevEventEntry> events;
  std::vector<FevSoundDefinition> definitions;
};
namespace fev_detail {
struct Cursor {
  AudioBytes b; size_t p=0;
  void require(bool ok) const {if(!ok)throw std::invalid_argument("Unsupported FEV1 event layout");}
  void skip(size_t n){require(p<=b.size()&&n<=b.size()-p);p+=n;}
  uint32_t u(){const auto at=p;skip(4);return AudioU32(b,at);}
  uint16_t u16(){const auto at=p;skip(2);return AudioU16(b,at);}
  uint32_t count(uint32_t max){const auto n=u();require(n<=max);return n;}
  std::string_view str(){const auto n=count(1024);const auto at=p;skip(n);if(!n)return {};const auto s=AudioText(b.subspan(at,n));require(s.back()==0);const auto t=s.substr(0,n-1);require(std::all_of(t.begin(),t.end(),[](unsigned char c){return c>=32&&c<127;}));return t;}
  void category(unsigned depth=0){require(depth<12);str();skip(16);const auto n=count(128);for(uint32_t i=0;i<n;++i)category(depth+1);}
  void properties(uint32_t n){for(uint32_t i=0;i<n;++i){str();const auto type=count(2);if(type==2)str();else skip(4);}}
};
}
// Original 0x002c0000 layout, not the later FEV1/RIFF format. A table is
// admitted only after its structured traversal reaches the verified footer
// and agrees exactly with the independent waveform record inventory.
inline std::optional<FevEventTable> InspectFevEventTable(AudioBytes b,std::string_view project) {
  const auto banks=InspectOriginalFevBanks(b,project);
  const auto waves=InspectFevWaveforms(b,project);
  if(!banks||!waves)return {};
  try {
    fev_detail::Cursor r{b,8};r.require(r.str()==project);r.require(r.u()==4);
    for(const auto& bank:*banks){r.skip(8);r.require(r.str()==bank.stem);}
    r.category();r.require(r.u()==2);FevEventTable table;
    for(auto expected:{"preload","nonpreload"}) {
      const auto group=r.str();r.require(group==expected);r.require(r.u()==0&&r.u()==0);
      const auto count=r.count(4096);
      for(uint32_t i=0;i<count;++i) {
        FevEventEntry event;event.group=group;event.name=r.str();r.require(!event.name.empty());
        r.skip(124);const auto layers=r.count(128);
        for(uint32_t l=0;l<layers;++l) {
          r.skip(6);const auto sounds=r.u16(),effects=r.u16();
          r.require(sounds<=4096&&effects<=128);event.has_effects|=effects!=0;
          std::vector<uint16_t> refs;refs.reserve(sounds);
          for(uint32_t k=0;k<sounds;++k){refs.push_back(r.u16());r.skip(64);}
          event.layers.push_back(std::move(refs));
          for(uint32_t k=0;k<effects;++k){r.skip(4);r.str();r.skip(8);r.skip(size_t(r.count(4096))*12+8);}
        }
        const auto parameters=r.count(1024);event.has_parameters=parameters!=0;
        for(uint32_t k=0;k<parameters;++k){r.str();r.skip(24);r.skip(size_t(r.count(4096))*4);}
        const auto properties=r.count(1024);event.has_properties=properties!=0;r.properties(properties);
        r.skip(4);event.category=r.str();table.events.push_back(std::move(event));
      }
    }
    const auto defs=r.count(16384);size_t observed=0;
    for(uint32_t i=0;i<defs;++i) {
      FevSoundDefinition def;def.name=r.str();r.skip(60);const auto count=r.count(4096);
      for(uint32_t k=0;k<count;++k) {
        const auto at=r.p;const auto type=r.u(),weight=r.u();
        if(type==2){def.variants.push_back(std::nullopt);continue;}
        r.require(type==0&&observed<waves->size());
        const auto path=r.str(),bank=r.str();const auto index=r.u(),duration=r.u();
        const auto& w=(*waves)[observed++];
        r.require(at==w.offset&&weight==w.weight&&path==w.path&&bank==(*banks)[w.bank].stem&&index==w.sample_ordinal&&duration==w.duration_ms);
        def.variants.push_back(at);
      }
      table.definitions.push_back(std::move(def));
    }
    r.require(observed==waves->size()&&r.u()==0&&r.p==b.size());
    for(const auto& e:table.events)for(const auto& l:e.layers)for(auto d:l)r.require(d<table.definitions.size());
    return table;
  }catch(const std::invalid_argument&){return {};}
}
} // namespace hulk::eor::audio
