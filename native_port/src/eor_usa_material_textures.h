#pragma once
#include "eor_xdk_guest_constants.h"
#include <algorithm>

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
    const std::array<uint32_t,16>& bound, ReadGuestBytes read, void* user) noexcept {
  OriginalPixelTextureKeys keys{};
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
      if (!bound[slot]) continue;
      std::array<std::byte,4> resource{}, xdk_object{};
      std::array<std::byte,24> asset{};
      if (!bytes(parameters,(uint64_t(packed>>16)+j)*16,resource) ||
          !bytes(MaterialWord(resource.data()),0,asset) ||
          MaterialWord(asset.data())!=0x82070CB8u || // ERTexture vtable in the pinned USA image
          !bytes(MaterialWord(asset.data()+20),24,xdk_object) ||
          MaterialWord(xdk_object.data())!=bound[slot]) continue;
      keys[slot]=MaterialWord(asset.data()+12);
    }
  }
  return keys;
}
inline OriginalPixelTextureKeys ReadUsaItemPixelTextures(
    const UsaAppliedMaterial& applied, uint32_t pass_context, uint32_t item_descriptor,
    const std::array<uint32_t,16>& bound, ReadGuestBytes read, void* user) noexcept {
  if (!applied.selector || !applied.parameters || applied.context!=pass_context ||
      applied.descriptor!=item_descriptor ||
      applied!=ReadUsaAppliedMaterial(pass_context,item_descriptor,read,user)) return {};
  return ReadUsaMaterialPixelTextures(pass_context,applied.selector,applied.parameters,bound,read,user);
}
} // namespace hulk::eor::xdk
