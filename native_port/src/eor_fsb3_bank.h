#pragma once
// Original EoR WaveBanks envelope and FEV1 v0x002c0000 bank directory.
// The returned sample bytes remain compressed. No codec or event ABI is assumed.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
namespace hulk::eor::audio {
using AudioBytes=std::span<const std::byte>;
inline uint32_t AudioU32(AudioBytes b,size_t p,bool be=false) noexcept {
  uint32_t v=0;
  for(unsigned i=0;i<4;++i)v|=uint32_t(std::to_integer<uint8_t>(b[p+i]))<<(8u*(be?3u-i:i));
  return v;
}
inline uint16_t AudioU16(AudioBytes b,size_t p) noexcept {
  return uint16_t(std::to_integer<uint8_t>(b[p]))|uint16_t(std::to_integer<uint8_t>(b[p+1]))<<8;
}
inline std::string_view AudioText(AudioBytes b) noexcept {
  return {reinterpret_cast<const char*>(b.data()),b.size()};
}
inline std::optional<uint32_t> AudioStemHash(std::string_view s) noexcept {
  if(s.empty() || s.size()>128)return {};
  uint32_t h=0xffffffffu;
  for(unsigned char c:s) {
    if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return {};
    if(c>='a'&&c<='z')c=static_cast<unsigned char>(c-32);
    h^=c;
    for(unsigned j=0;j<8;++j)h=(h>>1)^((h&1u)?0xedb88320u:0u);
  }
  return h^0xffffffffu;
}
struct Fsb3Sample {
  std::string_view name;
  uint32_t frames=0,mode=0,rate=0;
  uint16_t channels=0;
  AudioBytes payload;
};
struct OriginalWaveBank {
  std::string_view stem;
  uint32_t envelope_tag=0;
  std::vector<Fsb3Sample> samples;
};
inline std::optional<OriginalWaveBank> InspectOriginalWaveBank(
    AudioBytes resource,uint32_t expected_hash,bool big_endian_envelope) {
  if(resource.size()<33 || resource.size()>(64u<<20))return {};
  const auto text=AudioText(resource);
  const auto end=text.find('\0');
  if(end<5 || end>132 || end+9>resource.size() || text.substr(end-4,4)!=".fsb")return {};
  const auto stem=text.substr(0,end-4);
  if(AudioStemHash(stem)!=expected_hash || AudioU32(resource,end+1,big_endian_envelope)!=expected_hash)return {};
  const auto tag=AudioU32(resource,end+5,big_endian_envelope);
  if(tag!=1u && tag!=2u)return {};
  const auto b=resource.subspan(end+9);
  if(b.size()<24 || AudioText(b.first(4))!="FSB3" || AudioU32(b,16)!=0x30001u || AudioU32(b,20)!=0u)return {};
  const auto count=AudioU32(b,4),headers=AudioU32(b,8),bytes=AudioU32(b,12);
  if(!count || count>4096u || headers!=uint64_t(count)*80u+8u || 24ull+headers+bytes!=b.size())return {};
  OriginalWaveBank result{stem,tag,{}};
  result.samples.reserve(count);
  size_t data=24u+headers;
  for(uint32_t i=0;i<count;++i) {
    const size_t p=24u+80u*i;
    if(AudioU16(b,p)!=80u)return {};
    const auto name=AudioText(b.subspan(p+2,30));
    const auto len=name.find('\0');
    const auto frames=AudioU32(b,p+32),n=AudioU32(b,p+36),mode=AudioU32(b,p+48),rate=AudioU32(b,p+52);
    const auto channels=AudioU16(b,p+62);
    if(!len || !frames || !n || !rate || (channels!=1 && channels!=2) || n>b.size()-data)return {};
    result.samples.push_back({name.substr(0,len),frames,mode,rate,channels,b.subspan(data,n)});
    data+=n;
  }
  if(data!=b.size())return {};
  return result;
}
struct OriginalFevBankReference {std::string_view stem;uint32_t hash=0,flags=0,opaque=0;};
inline std::optional<std::vector<OriginalFevBankReference>> InspectOriginalFevBanks(
    AudioBytes fev,std::string_view project) {
  if(fev.size()<16 || AudioText(fev.first(4))!="FEV1" || AudioU32(fev,4)!=0x002c0000u || !AudioStemHash(project))return {};
  const auto text=AudioText(fev);const auto n=AudioU32(fev,8);
  if(n!=project.size()+1u || 12ull+n+4u>fev.size() || text.substr(12,n-1)!=project || text[12+n-1]!=0)return {};
  size_t p=12u+n;
  if(AudioU32(fev,p)!=4u)return {};
  p+=4;
  std::vector<OriginalFevBankReference> refs;
  for(unsigned i=0;i<4;++i) {
    if(fev.size()-p<12)return {};
    const auto flags=AudioU32(fev,p),opaque=AudioU32(fev,p+4),len=AudioU32(fev,p+8);p+=12;
    if(len<2 || len>129 || len>fev.size()-p || text[p+len-1]!=0)return {};
    const auto name=text.substr(p,len-1);p+=len;
    if(!name.starts_with(project) || name.size()<=project.size() || name[project.size()]!='_')return {};
    const auto hash=AudioStemHash(name);
    if(!hash)return {};
    for(const auto& ref:refs)if(ref.hash==*hash)return {};
    refs.push_back({name,*hash,flags,opaque});
  }
  return refs;
}
} // namespace hulk::eor::audio
