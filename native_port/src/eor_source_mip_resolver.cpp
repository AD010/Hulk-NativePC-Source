#include "eor_source_mip_resolver.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>
namespace hulk::eor::gpu {
namespace {
using W=OriginalMipWitness;
auto Key(const W& w) {
  return std::tie(w.xenos_format,w.width,w.height,w.full_xxh3,
                  w.first_fnv,w.second_fnv);
}
bool Less(const OriginalMipIdentity& a,const OriginalMipIdentity& b) {
  return Key(a.witness)<Key(b.witness);
}
bool Valid(const OriginalMipIdentity& entry) {
  const auto& w=entry.witness;
  return entry.original_texture_hash && w.width && w.height &&
    w.width<=8192 && w.height<=8192 && w.full_xxh3 &&
    (w.xenos_format==2 || w.xenos_format==6 || w.xenos_format==18 ||
     w.xenos_format==20 || w.xenos_format==29) &&
    entry.original_mip_level<16;
}
}
VerifiedSourceMipResolver::VerifiedSourceMipResolver(
    std::span<const OriginalMipIdentity> source) {
  if(source.size()>131072)
    throw std::invalid_argument("Oversized original Xbox mip witness catalog");
  original_.assign(source.begin(),source.end());
  for(const auto& item:original_)
    if(!Valid(item))
      throw std::invalid_argument("Unverified original Xbox mip witness metadata");
  std::sort(original_.begin(),original_.end(),Less);
  for(size_t i=0;i<original_.size();) {
    size_t j=i+1;
    while(j<original_.size() && Key(original_[i].witness)==Key(original_[j].witness))
      ++j;
    uint32_t owner=original_[i].original_texture_hash;
    for(size_t k=i+1;k<j;++k)
      if(original_[k].original_texture_hash!=owner) {
        ++ambiguous_;break;
      }
    i=j;
  }
}
std::optional<OriginalMipIdentity> VerifiedSourceMipResolver::Find(
    const OriginalMipWitness& observed) const {
  if(!observed.xenos_format || !observed.width || !observed.height ||
     !observed.full_xxh3)
    return std::nullopt;
  const auto key=Key(observed);
  auto it=std::lower_bound(original_.begin(),original_.end(),observed,
      [](const auto& item,const auto& target){
        return Key(item.witness)<Key(target);
      });
  if(it==original_.end() || Key(it->witness)!=key)
    return std::nullopt;
  auto first=it;
  while(it!=original_.end() && Key(it->witness)==key) {
    if(it->original_texture_hash!=first->original_texture_hash ||
       it->original_mip_level!=first->original_mip_level ||
       it->archived_stream!=first->archived_stream)
      return std::nullopt;
    ++it;
  }
  return *first;
}
} // namespace hulk::eor::gpu
