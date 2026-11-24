#pragma once
#include "eor_xdk_guest_constants.h"
#include <algorithm>
#include <map>

namespace hulk::eor::xdk {
using OriginalPixelTextureKeys = std::array<uint32_t,16>;

struct UsaAppliedMaterial {
  uint32_t context=0, descriptor=0, selector=0, parameters=0;
  bool operator==(const UsaAppliedMaterial&) const = default;
};

inline uint32_t MaterialWord(const std::byte* p) noexcept {
  return uint32_t(std::to_integer<uint8_t>(p[0]))<<24 |
         uint32_t(std::to_integer<uint8_t>(p[1]))<<16 |
         uint32_t(std::to_integer<uint8_t>(p[2]))<<8 |
         uint32_t(std::to_integer<uint8_t>(p[3]));
}

// An archive identity is recoverable outside the original material scope
// only by re-reading the SAME ERTexture -> wrapper -> live XDK object.
struct OriginalTextureOwner {
  uint32_t asset=0,wrapper=0,xdk=0,hash=0;
  bool operator==(const OriginalTextureOwner&) const=default;
};
using OriginalTextureOwners=std::array<OriginalTextureOwner,16>;
inline std::optional<OriginalTextureOwner> ReadOriginalTextureOwner(
    uint32_t asset,uint32_t bound,ReadGuestBytes read,void* user) noexcept {
  std::array<std::byte,24> data{};
  std::array<std::byte,4> object{};
  if(!read || !asset || !bound || uint64_t(asset)+data.size()>(uint64_t{1}<<32) ||
     !read(user,asset,data.data(),data.size()) || MaterialWord(data.data())!=0x82070CB8u)return {};
  const auto wrapper=MaterialWord(data.data()+20),hash=MaterialWord(data.data()+12);
  if(!hash || !wrapper || uint64_t(wrapper)+28u>(uint64_t{1}<<32) ||
     !read(user,wrapper+24u,object.data(),object.size()) ||
     MaterialWord(object.data())!=bound)return {};
  return OriginalTextureOwner{asset,wrapper,bound,hash};
}
// Only previously observed real material bindings can seed this table.
// Revalidate on lookup. Distinct live ERTexture owners for the same XDK
// object with different resource hashes are ambiguous, never select one.
class OriginalTextureOwnerCache {
 public:
  void Reset() noexcept {owners_.clear();}
  void Remember(const OriginalTextureOwner& owner,ReadGuestBytes read,void* user) noexcept {
    if(!owner.asset || !owner.hash || !owner.xdk)return;
    auto current=ReadOriginalTextureOwner(owner.asset,owner.xdk,read,user);
    if(!current || *current!=owner)return;
    auto it=owners_.find(owner.xdk);
    if(it==owners_.end()) {
      if(owners_.size()>=8192u)return;
      try {owners_.emplace(owner.xdk,Entry{owner,false});}
      catch(...) {} // optional capture must never terminate gameplay
      return;
    }
    auto& entry=it->second;
    if(entry.ambiguous)return;
    const auto previous=ReadOriginalTextureOwner(entry.owner.asset,owner.xdk,read,user);
    if(previous && *previous==entry.owner && entry.owner.hash!=owner.hash)
      entry.ambiguous=true;
    else entry.owner=owner;
  }
  std::optional<OriginalTextureOwner> Find(uint32_t bound,ReadGuestBytes read,void* user) const noexcept {
    const auto it=owners_.find(bound);
    if(it==owners_.end() || it->second.ambiguous)return {};
    const auto current=ReadOriginalTextureOwner(it->second.owner.asset,bound,read,user);
    return current && *current==it->second.owner ? current : std::nullopt;
  }
 private:
  struct Entry {OriginalTextureOwner owner;bool ambiguous=false;};
  std::map<uint32_t,Entry> owners_;
};
// 0x825F8A50 passes descriptor[0] and descriptor[2] to 0x825F87B0.
inline UsaAppliedMaterial ReadUsaAppliedMaterial(uint32_t context, uint32_t descriptor,
                                                  ReadGuestBytes read, void* user) noexcept {
  std::array<std::byte,12> words{};
  if (!context || !descriptor || !read || uint64_t(descriptor)+words.size()>(uint64_t{1}<<32) ||
      !read(user,descriptor,words.data(),words.size())) return {};
  return {context,descriptor,MaterialWord(words.data()),MaterialWord(words.data()+8)};
}

// USA 0x825F87B0 selects a pixel binding row. 0x825F9D48 walks its
// texture groups and 0x82675DD0 binds ERTexture -> wrapper -> XDK texture.
inline OriginalPixelTextureKeys ReadUsaMaterialPixelTextures(
    uint32_t material, uint32_t selector, uint32_t parameters,
    const std::array<uint32_t,16>& bound, ReadGuestBytes read, void* user,
    OriginalTextureOwners* witnesses=nullptr) noexcept {
  OriginalPixelTextureKeys keys{};
  if(witnesses)*witnesses={};
  OriginalTextureOwners captured{};
  if (!read || !material || !selector || !parameters) return keys;
  const auto bytes = [&](uint32_t base, uint64_t offset, auto& out) {
    if (!base || uint64_t(base)+offset+out.size()>uint64_t(UINT32_MAX)+1) return false;
    return read(user,static_cast<uint32_t>(base+offset),out.data(),out.size());
  };
  std::array<std::byte,4> selected{};
  std::array<std::byte,137> context{}; // +99 through +235
  if (!bytes(selector,0,selected) || !bytes(material,99,context)) return keys;
  const auto field = [&](size_t offset) { return MaterialWord(context.data()+offset-99); };
  const uint64_t row = uint64_t(std::to_integer<uint8_t>(context[0]))*field(232)+
                       MaterialWord(selected.data());
  if (row>=field(188)) return keys;
  std::array<std::byte,12> record{};
  if (!bytes(field(184),row*12,record)) return keys;
  const uint64_t first=uint64_t(MaterialWord(record.data()))+std::to_integer<uint8_t>(record[8]);
  const uint32_t groups=std::to_integer<uint8_t>(record[9]);
  if (first+groups>field(164)) return keys;
  for (uint32_t i=0;i<groups;++i) {
    std::array<std::byte,4> group{};
    if (!bytes(field(160),(first+i)*4,group)) return {};
    const uint32_t packed=MaterialWord(group.data());
    const uint32_t first_slot=packed&511u;
    if (first_slot>=bound.size()) continue;
    const uint32_t count=std::min<uint32_t>((packed>>9)&127u,16u-first_slot);
    for (uint32_t j=0;j<count;++j) {
      const uint32_t slot=first_slot+j;
      keys[slot]=0; // a later group overwrites this slot, even for a raw texture
      captured[slot]={};
      if (!bound[slot]) continue;
      std::array<std::byte,4> resource{};
      if(!bytes(parameters,(uint64_t(packed>>16)+j)*16,resource))continue;
      const auto owner=ReadOriginalTextureOwner(MaterialWord(resource.data()),bound[slot],read,user);
      if(!owner)continue;
      keys[slot]=owner->hash;
      captured[slot]=*owner;
    }
  }
  if(witnesses)*witnesses=captured;
  return keys;
}
inline OriginalPixelTextureKeys ReadUsaItemPixelTextures(
    const UsaAppliedMaterial& applied, uint32_t pass_context, uint32_t item_descriptor,
    const std::array<uint32_t,16>& bound, ReadGuestBytes read, void* user,
    OriginalTextureOwners* witnesses=nullptr) noexcept {
  if(witnesses)*witnesses={};
  if (!applied.selector || !applied.parameters || applied.context!=pass_context ||
      applied.descriptor!=item_descriptor ||
      applied!=ReadUsaAppliedMaterial(pass_context,item_descriptor,read,user)) return {};
  return ReadUsaMaterialPixelTextures(pass_context,applied.selector,applied.parameters,bound,read,user,witnesses);
}
} // namespace hulk::eor::xdk
