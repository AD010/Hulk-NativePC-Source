// Native source-verified Xbox XDK shader sampler register declarations.
// HEORSMP1 is a PRIVATE metadata-only catalog from original ShaderEffects.
// It does not assert that XDK SetTexture slots == Xenos XeTexture fetch IDs.
#pragma once
#include "eor_spirv_bank.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace hulk::eor::aot {
struct OriginalXdkShaderSampler {
  uint32_t original_sampler_register=UINT32_MAX;
  std::string original_sampler_name;
};
class OriginalXdkShaderSamplers {
 public:
  void Load(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)throw std::invalid_argument("Original private HEORSMP1 catalog missing");
    const auto length=f.tellg();
    if(length<16 || length>16ll+72ll*131072)
      throw std::invalid_argument("Unsafe original XDK sampler catalog size");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size())))
      throw std::invalid_argument("Truncated original Xbox shader sampler bank");
    if(std::string(bytes.begin(),bytes.begin()+8)!="HEORSMP1")
      throw std::invalid_argument("Unknown original shader sampler bank format");
    const auto u32=[&](size_t p) noexcept -> uint32_t {
      return uint32_t(bytes[p])|(uint32_t(bytes[p+1])<<8)|
             (uint32_t(bytes[p+2])<<16)|(uint32_t(bytes[p+3])<<24);
    };
    const auto count=u32(8),version=u32(12);
    if(version!=1 || count==0 || count>131072 ||
       bytes.size()!=16ull+72ull*count)
      throw std::invalid_argument("Invalid original sampler bank version/count");
    struct Entry {Stage stage;std::vector<OriginalXdkShaderSampler> slots;
                  bool empty_marker=false;};
    std::map<Digest,Entry> verified;
    Digest previous{};
    uint32_t previous_register=0;
    bool previous_valid=false;
    for(uint32_t i=0;i<count;++i) {
      const size_t base=16ull+size_t(i)*72;
      Digest id{};
      for(size_t j=0;j<id.size();++j)id[j]=bytes[base+j];
      const auto stage=u32(base+32),reg=u32(base+36);
      if((stage!=1 && stage!=2) || id==Digest{})
        throw std::invalid_argument("Missing original source shader ID/stage");
      if(previous_valid &&
         (id<previous || (id==previous && reg<=previous_register)))
        throw std::invalid_argument("Original sampler catalog records not ordered");
      previous=id;previous_register=reg;previous_valid=true;
      std::string name;
      bool nul=false;
      for(size_t j=0;j<32;++j) {
        const uint8_t c=bytes[base+40+j];
        if(!c){nul=true;continue;}
        if(nul || c<0x21 || c>0x7e || c=='/' || c=='\\')
          throw std::invalid_argument("Unsafe XDK sampler source name/padding");
        name+=static_cast<char>(c);
      }
      if(!nul)
        throw std::invalid_argument("Unterminated original shader sampler name");
      auto [it,created]=verified.try_emplace(
          id,Entry{static_cast<Stage>(stage),{},false});
      auto& shader=it->second;
      if(!created && shader.stage!=static_cast<Stage>(stage))
        throw std::invalid_argument("Original shader changes stage");
      if(reg==UINT32_MAX) {
        if(!name.empty() || !created || !shader.slots.empty())
          throw std::invalid_argument("Invalid original no-sampler sentinel");
        shader.empty_marker=true;
      } else {
        if(reg>=32 || name.empty() || shader.empty_marker)
          throw std::invalid_argument("Unknown original source sampler register");
        if((name[0]<'A'||name[0]>'Z') &&
           (name[0]<'a'||name[0]>'z') && name[0]!='_')
          throw std::invalid_argument("Invalid source shader sampler identifier");
        for(const char c:name) {
          if((c<'A'||c>'Z') && (c<'a'||c>'z') &&
             (c<'0'||c>'9') && c!='_')
            throw std::invalid_argument("Invalid original sampler variable identifier");
        }
        for(const auto& old:shader.slots)
          if(old.original_sampler_register==reg ||
             old.original_sampler_name==name)
            throw std::invalid_argument("Ambiguous original XDK sampler declaration");
        shader.slots.push_back({reg,std::move(name)});
      }
    }
    std::map<Digest,SourceSamplers> next;
    for(auto& [sha,entry]:verified)
      next.emplace(sha,SourceSamplers{entry.stage,std::move(entry.slots)});
    groups_.swap(next); // Broken bank never replaces a valid prior one.
  }
  const std::vector<OriginalXdkShaderSampler>* Find(
      const Digest& sha,Stage expected) const noexcept {
    const auto it=groups_.find(sha);
    return it!=groups_.end() && it->second.stage==expected ?
        &it->second.samplers:nullptr;
  }
  size_t ShaderCount() const noexcept {return groups_.size();}
 private:
  struct SourceSamplers {
    Stage stage=Stage::Vertex;
    std::vector<OriginalXdkShaderSampler> samplers;
  };
  std::map<Digest,SourceSamplers> groups_;
};
} // namespace hulk::eor::aot
