#pragma once
// Source-only HEORCND1 Xbox archive model->surface->texture candidate reader.
// NEVER use the results as a source-verified Vulkan material or XDK Draw.
// The caller must independently authenticate guest VB/IB, shaders, sampler,
// render targets, original render-state and the real SPIR-V PSO.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace hulk::eor::world {
struct OriginalSurfaceCandidate {
  uint32_t original_model_hash=0;
  uint32_t original_surface_ordinal=0;
  bool operator<(const OriginalSurfaceCandidate& b) const noexcept {
    return std::tie(original_model_hash,original_surface_ordinal)<
           std::tie(b.original_model_hash,b.original_surface_ordinal);
  }
  bool operator==(const OriginalSurfaceCandidate&) const = default;
};

class OriginalMaterialCandidateIndex {
 public:
  OriginalMaterialCandidateIndex()=default;
  void Load(const std::filesystem::path& file) {
    std::ifstream in(file,std::ios::binary|std::ios::ate);
    if(!in)throw std::invalid_argument("Missing original material candidate index");
    const auto size=in.tellg();
    constexpr size_t kHeader=8+4*4+32*3;
    if(size<0 || static_cast<uint64_t>(size)<kHeader ||
       static_cast<uint64_t>(size)>8u*1024u*1024u)
      throw std::invalid_argument("Invalid original material candidate bank length");
    std::vector<uint8_t> raw(static_cast<size_t>(size));
    in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(raw.data()),raw.size()))
      throw std::invalid_argument("Original material candidate bank truncated");
    const char magic[8]={'H','E','O','R','C','N','D','1'};
    if(!std::equal(raw.begin(),raw.begin()+8,magic))
      throw std::invalid_argument("Invalid original material candidate magic");
    const auto read32=[&](size_t off) noexcept {
      return uint32_t(raw[off])|(uint32_t(raw[off+1])<<8)|
             (uint32_t(raw[off+2])<<16)|(uint32_t(raw[off+3])<<24);
    };
    const uint32_t version=read32(8),models=read32(12),surfaces=read32(16),
                   count=read32(20);
    if(version!=1 || models!=4174 || surfaces!=11622 || count==0 ||
       count>100000 || raw.size()!=kHeader+size_t(count)*12u)
      throw std::invalid_argument("Original USA Xbox source census mismatch");
    std::array<uint8_t,32> source_index{};
    for(size_t i=0;i<32;++i)source_index[i]=raw[24+i];
    if(std::all_of(source_index.begin(),source_index.end(),
                   [](uint8_t b){return b==0;}))
      throw std::invalid_argument("Original material source index is unpinned");
    std::vector<Link> next;
    next.reserve(count);
    for(uint32_t i=0;i<count;++i) {
      const size_t off=kHeader+size_t(i)*12u;
      const Link link{read32(off),{read32(off+4),read32(off+8)}};
      if(!link.texture || !link.owner.original_model_hash ||
         link.owner.original_surface_ordinal>=surfaces ||
         (!next.empty() && !(next.back()<link)))
        throw std::invalid_argument("Original candidate source is unsorted/duplicated");
      next.push_back(link);
    }
    source_index_=source_index;
    entries_=std::move(next);
  }
  std::array<uint8_t,32> OriginalIndexDigest() const noexcept {
    return source_index_;
  }
  size_t LinkCount() const noexcept {return entries_.size();}
  // Input must be captured from the SAME real game Draw and source pass.
  // Unmapped source texture keys are not replaced with guessed archive IDs.
  // Bounded output is only an archive MODEL REQUEST hint, not proof of owner.
  std::vector<OriginalSurfaceCandidate> FindLiveSurfaceCandidates(
      const std::array<uint32_t,16>& original_pixel_texture_keys,
      uint16_t verified_mask,uint32_t exact_model_hash=0,
      size_t max_matches=64) const {
    if(!verified_mask || !max_matches || entries_.empty())return {};
    std::set<uint32_t> hashes;
    for(size_t slot=0;slot<16;++slot) {
      if(!(verified_mask&(uint16_t{1}<<slot)))continue;
      if(!original_pixel_texture_keys[slot])return {};
      hashes.insert(original_pixel_texture_keys[slot]);
    }
    if(hashes.empty())return {};
    std::vector<OriginalSurfaceCandidate> intersection;
    bool first=true;
    for(uint32_t texture:hashes) {
      const auto lower=std::lower_bound(entries_.begin(),entries_.end(),texture,
          [](const Link& entry,uint32_t hash){return entry.texture<hash;});
      const auto upper=std::upper_bound(lower,entries_.end(),texture,
          [](uint32_t hash,const Link& entry){return hash<entry.texture;});
      std::vector<OriginalSurfaceCandidate> owners;
      for(auto it=lower;it!=upper;++it)
        if(!exact_model_hash || it->owner.original_model_hash==exact_model_hash)
          owners.push_back(it->owner);
      if(first) {
        intersection=std::move(owners);
        first=false;
      } else {
        std::vector<OriginalSurfaceCandidate> common;
        std::set_intersection(intersection.begin(),intersection.end(),
                              owners.begin(),owners.end(),
                              std::back_inserter(common));
        intersection=std::move(common);
      }
      if(intersection.empty())break;
    }
    if(intersection.size()>max_matches)return {};
    return intersection;
  }
 private:
  struct Link {
    uint32_t texture=0;
    OriginalSurfaceCandidate owner{};
    bool operator<(const Link& b) const noexcept {
      return std::tie(texture,owner.original_model_hash,
                      owner.original_surface_ordinal)<
             std::tie(b.texture,b.owner.original_model_hash,
                      b.owner.original_surface_ordinal);
    }
  };
  std::array<uint8_t,32> source_index_{};
  std::vector<Link> entries_;
};
} // namespace hulk::eor::world
