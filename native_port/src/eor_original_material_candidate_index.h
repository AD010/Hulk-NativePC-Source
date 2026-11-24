#pragma once
// Source-only HEORCND1 Xbox archive model->surface->texture candidate reader.
// NEVER use the results as a source-verified Vulkan material or XDK Draw.
// The caller must independently authenticate guest VB/IB, shaders, sampler,
// render targets, original render-state and the real SPIR-V PSO.
#include "eor_original_model_assets.h"
#include "eor_original_virtual_shader_identity.h"
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
  // Build the very same private Xbox source candidate index directly
  // from the USER's actual USA INDEX.IND and MODELS.ARC. Both accepted USA
  // index variants are pinned by full SHA-256 in Index::LoadPinnedUsaModelArchive.
  // No HEORCND1 sidecar, precomputed model-name guesses or separate GPU
  // renderer is required. A source texture match still only PREFETCHES
  // archive models, it never asserts a real XDK Draw/material/PSO.
  void LoadFromOriginalXboxArchive(const std::filesystem::path& index_path,
                                   const std::filesystem::path& models_path) {
    const auto source_index=Index::LoadPinnedUsaModelArchive(index_path);
    const auto group=source_index.groups().find("Models");
    if(group==source_index.groups().end() || group->second.size()!=4174 ||
       source_index.groups().find("Textures")==source_index.groups().end() ||
       source_index.groups().at("Textures").size()!=6669)
      throw std::invalid_argument("Incomplete pinned original USA material archive index");
    const auto source=model::OriginalModelAssets::Open(source_index,models_path);
    if(source.IndexedModelCount()!=4174)
      throw std::invalid_argument("Incomplete original EoR model archive census");
    std::vector<Link> next;
    next.reserve(41745);
    uint32_t total_surfaces=0;
    for(const auto& item:group->second) {
      auto original=source.LoadWithMaterialsHash(item.hash);
      if(!original || original->model.original_resource_hash!=item.hash)
        throw std::invalid_argument("Missing authentic indexed original Xbox model");
      for(const auto& surface:original->surfaces) {
        if(surface.ordinal>=original->surfaces.size() ||
           surface.ordinal!=(&surface-original->surfaces.data()) ||
           surface.original_material_name.empty())
          throw std::invalid_argument("Original Xbox material surface order broken");
        ++total_surfaces;
        for(const auto& texture:surface.indexed_texture_references) {
          if(texture.original_resource.hash!=texture.original_texture_hash ||
             !texture.original_texture_hash || !texture.original_resource.size ||
             texture.original_resource.reserved)
            throw std::invalid_argument("Original Xbox texture source binding invalid");
          next.push_back(Link{
              texture.original_texture_hash,
              OriginalSurfaceCandidate{item.hash,surface.ordinal}});
        }
      }
    }
    std::sort(next.begin(),next.end());
    next.erase(std::unique(next.begin(),next.end(),
        [](const Link& a,const Link& b){return !(a<b)&&!(b<a);}),next.end());
    if(total_surfaces!=11622 || next.size()!=41745)
      throw std::invalid_argument(
          "USA MODELS.ARC material/texture source census changed");
    // SHA digest of the SAME pinned original index file that authorized
    // this complete live archive. Never replace a good loaded candidate
    // map with a partially scanned/corrupt archive.
    std::ifstream file(index_path,std::ios::binary|std::ios::ate);
    if(!file || file.tellg()!=534176)
      throw std::invalid_argument("Wrong source index during material generation");
    std::vector<uint8_t> bytes(534176);
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))
      throw std::invalid_argument("Truncated source index during material generation");
    const auto digest=aot::SourceSha256(bytes);
    source_index_=digest;
    entries_=std::move(next);
  }
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
  // ARCHIVE PREFETCH ONLY: a shared diffuse/normal texture can belong to
  // hundreds of original buildings. The strict unique-owner resolver above
  // must keep rejecting ambiguous ownership, but prefetching NO models
  // made recovery of the original VB/IB owner impossible. Page through
  // the real indexed model/surface matches; byte-exact guest geometry,
  // ShaderEffects and current Draw still separately prove any GPU owner.
  std::vector<OriginalSurfaceCandidate> PrefetchLiveSurfaceCandidatesPage(
      const std::array<uint32_t,16>& live_source_textures,
      uint16_t source_slots,size_t requested_page,size_t page_size=12) const {
    if(!page_size || page_size>128 || entries_.empty())return {};
    auto full=FindLiveSurfaceCandidates(live_source_textures,source_slots,0,
                                        entries_.size());
    if(full.empty())return {};
    const size_t pages=1+(full.size()-1)/page_size;
    const size_t from=(requested_page%pages)*page_size;
    const size_t count=std::min(page_size,full.size()-from);
    return std::vector<OriginalSurfaceCandidate>(
        full.begin()+from,full.begin()+from+count);
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
