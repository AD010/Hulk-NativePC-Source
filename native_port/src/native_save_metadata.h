#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hulk::save {
// Host-only metadata. No guest XUID, device, title ID or license fields.
// Explicit LE UTF-16 and CRC32, independent of host pointer width/alignment.
inline constexpr std::array<uint8_t,8> kNativeMetadataMagic{'H','U','L','K','P','C','M',1};
inline constexpr size_t kNativeMetadataMaximum=8+2+127*2+4;
inline bool IsHulkSaveSlot(std::string_view name) noexcept {
  constexpr std::string_view expected="HULK.SAV";
  if(name.size()!=expected.size())return false;
  for(size_t i=0;i<name.size();++i){
    char c=name[i];if(c>='a'&&c<='z')c-=32;
    if(c!=expected[i])return false;
  }
  return true;
}
inline bool ValidMetadataName(std::u16string_view s) noexcept {
  if(s.size()>127)return false;
  for(size_t i=0;i<s.size();++i){
    const auto c=s[i];if(!c)return false;
    if(c>=0xd800&&c<=0xdbff){
      if(++i==s.size()||s[i]<0xdc00||s[i]>0xdfff)return false;
    }else if(c>=0xdc00&&c<=0xdfff)return false;
  }
  return true;
}
inline uint32_t NativeMetadataCrc(const uint8_t* p,size_t n) noexcept {
  uint32_t crc=0xffffffffu;
  for(size_t i=0;i<n;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);}
  return crc^0xffffffffu;
}
// The supplied USA recomp save importer independently established this
// exact framing: a BE CRC32 at +0, coverage beginning at +12. Bytes 4..11
// and the internal progress fields remain opaque, not guessed PC fields.
inline constexpr size_t kLegacyProgressMinimum=64;
inline constexpr size_t kLegacyProgressMaximum=16u*1024u*1024u;
inline bool ValidLegacyProgress(const uint8_t* bytes,size_t size) noexcept {
  if(!bytes||size<kLegacyProgressMinimum||size>kLegacyProgressMaximum)return false;
  const uint32_t stored=(uint32_t(bytes[0])<<24)|(uint32_t(bytes[1])<<16)|
                        (uint32_t(bytes[2])<<8)|uint32_t(bytes[3]);
  return stored==NativeMetadataCrc(bytes+12,size-12);
}
inline std::optional<std::vector<uint8_t>> EncodeNativeMetadata(std::u16string_view name) {
  if(!ValidMetadataName(name))return {};
  std::vector<uint8_t> out(kNativeMetadataMagic.begin(),kNativeMetadataMagic.end());
  out.push_back(static_cast<uint8_t>(name.size()));out.push_back(0);
  for(auto c:name){out.push_back(static_cast<uint8_t>(c));out.push_back(static_cast<uint8_t>(c>>8));}
  const uint32_t crc=NativeMetadataCrc(out.data(),out.size());
  for(unsigned i=0;i<4;++i)out.push_back(static_cast<uint8_t>(crc>>(8*i)));
  return out;
}
inline std::optional<std::u16string> DecodeNativeMetadata(const uint8_t* bytes,size_t size) {
  if(!bytes||size<14||size>kNativeMetadataMaximum)return {};
  for(size_t i=0;i<kNativeMetadataMagic.size();++i)if(bytes[i]!=kNativeMetadataMagic[i])return {};
  const size_t count=bytes[8];if(bytes[9]||count>127||size!=14+2*count)return {};
  uint32_t crc=0;for(unsigned i=0;i<4;++i)crc|=uint32_t(bytes[size-4+i])<<(8*i);
  if(crc!=NativeMetadataCrc(bytes,size-4))return {};
  std::u16string name;name.reserve(count);
  for(size_t i=0;i<count;++i)name.push_back(static_cast<char16_t>(uint16_t(bytes[10+2*i])|(uint16_t(bytes[11+2*i])<<8)));
  if(!ValidMetadataName(name))return {};
  return name;
}
} // namespace hulk::save
