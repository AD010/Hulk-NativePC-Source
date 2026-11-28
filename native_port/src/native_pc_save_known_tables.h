#pragma once
// Layout evidenced by both original user save samples, not a semantic
// conversion of object references. Reject unknown table schemas. Preserve
// every byte outside the five observed indexed tables, including padding.
#include "native_pc_save_format.h"
#include <array>
#include <stdexcept>

namespace hulk::save::format {
struct KnownTables {
  Kind kind=Kind::XboxBigEndian;
  std::array<std::vector<FieldWord>,5> tables;
  std::array<std::vector<std::byte>,6> opaque;
};
namespace known_detail {
inline constexpr std::array<size_t,5> counts{242,280,50,256,25};
inline constexpr size_t maximum=16u<<20;
inline uint32_t Word(std::span<const std::byte> b,size_t at,Kind k) {
  if(at>b.size()||b.size()-at<4)throw std::invalid_argument("Truncated Hulk table word");
  uint32_t v=0;
  for(unsigned i=0;i<4;++i)
    v|=uint32_t(std::to_integer<uint8_t>(b[at+i]))<<(8*(k==Kind::XboxBigEndian?3-i:i));
  return v;
}
inline void Put(std::vector<std::byte>& b,size_t at,uint32_t v,Kind k) {
  for(unsigned i=0;i<4;++i)b[at+i]=std::byte((v>>(8*(k==Kind::XboxBigEndian?3-i:i)))&255u);
}
inline uint32_t Crc(std::span<const std::byte> b) noexcept {
  uint32_t c=0xffffffffu;
  for(auto x:b){c^=std::to_integer<uint8_t>(x);for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1u)?0xedb88320u:0);}
  return c^0xffffffffu;
}
inline uint64_t KeyFingerprint(std::span<const FieldWord> fields) noexcept {
  uint64_t h=0xcbf29ce484222325ull;
  for(const auto& f:fields)for(unsigned i=0;i<4;++i){h^=(f.id>>(8*i))&255u;h*=0x100000001b3ull;}
  return h;
}
inline std::array<uint64_t,5> Fingerprints(Kind k) noexcept {
  return {0xeab7a153925a7d69ull,0x3bb813ecf545956full,0xa8cee555ef61b22cull,
    k==Kind::XboxBigEndian?0x5522f2ab1ec159f6ull:0x8e639a53962dffdfull,0xd73bd4e351ca492aull};
}
}
inline KnownTables DecodeKnownTables(std::span<const std::byte> bytes,Kind kind) {
  using namespace known_detail;
  if((kind!=Kind::XboxBigEndian&&kind!=Kind::RetailPcLittleEndian)||bytes.size()<256||bytes.size()>maximum)
    throw std::invalid_argument("Unknown Hulk save format");
  if(Word(bytes,0,kind)!=Crc(bytes.subspan(12)))throw std::invalid_argument("Hulk save CRC mismatch");
  const size_t end=Word(bytes,4,kind),first=kind==Kind::XboxBigEndian?20u:16u;
  const size_t second=first+counts[0]*8+1,third=second+counts[1]*8+44;
  if(end>bytes.size()||end<2524||end-2524<third+counts[2]*8||bytes[first+counts[0]*8]!=std::byte{0})
    throw std::invalid_argument("Unknown Hulk indexed table boundaries");
  const std::array<size_t,5> offsets{first,second,third,end-2524,end-200};
  const auto fingerprints=Fingerprints(kind);
  KnownTables out;out.kind=kind;
  size_t cursor=0;
  for(size_t t=0;t<counts.size();++t){
    const auto at=offsets[t];
    if(at<cursor||at>bytes.size()||counts[t]*8>bytes.size()-at)
      throw std::invalid_argument("Hulk indexed table overlaps or exceeds save");
    out.opaque[t].assign(bytes.begin()+cursor,bytes.begin()+at);
    auto& table=out.tables[t];table.reserve(counts[t]);
    uint32_t previous=0;
    for(size_t i=0;i<counts[t];++i){
      const auto p=at+i*8;
      const auto id=Word(bytes,p,kind);
      if(id<=previous)throw std::invalid_argument("Unordered Hulk indexed table");
      table.push_back({id,Word(bytes,p+4,kind)});previous=id;
    }
    if(KeyFingerprint(table)!=fingerprints[t])throw std::invalid_argument("Unknown Hulk indexed table schema");
    cursor=at+counts[t]*8;
  }
  out.opaque.back().assign(bytes.begin()+cursor,bytes.end());
  return out;
}
inline std::vector<std::byte> EncodeKnownTables(const KnownTables& save) {
  using namespace known_detail;
  const auto k=save.kind;
  if(k!=Kind::XboxBigEndian&&k!=Kind::RetailPcLittleEndian)throw std::invalid_argument("Unknown Hulk save platform");
  const auto fingerprints=Fingerprints(k);
  const size_t first=k==Kind::XboxBigEndian?20u:16u;
  if(save.opaque[0].size()!=first||save.opaque[1].size()!=1||save.opaque[1][0]!=std::byte{0}||save.opaque[2].size()!=44||save.opaque[4].size()!=276)
    throw std::invalid_argument("Hulk indexed table layout changed");
  size_t total=0;
  for(const auto& part:save.opaque){if(part.size()>maximum-total)throw std::invalid_argument("Hulk save too large");total+=part.size();}
  for(size_t t=0;t<counts.size();++t){
    if(save.tables[t].size()!=counts[t]||KeyFingerprint(save.tables[t])!=fingerprints[t]||counts[t]*8>maximum-total)
      throw std::invalid_argument("Hulk indexed table schema changed");
    total+=counts[t]*8;
  }
  std::vector<std::byte> bytes;bytes.reserve(total);
  for(size_t t=0;t<counts.size();++t){
    bytes.insert(bytes.end(),save.opaque[t].begin(),save.opaque[t].end());
    for(const auto& f:save.tables[t]){const auto at=bytes.size();bytes.resize(at+8);Put(bytes,at,f.id,k);Put(bytes,at+4,f.raw_value,k);}
  }
  const size_t end=bytes.size();
  bytes.insert(bytes.end(),save.opaque.back().begin(),save.opaque.back().end());
  // Do not invent a new payload length or translate platform-specific data.
  if(Word(bytes,4,k)!=end)throw std::invalid_argument("Hulk opaque payload extent changed");
  Put(bytes,0,Crc(std::span<const std::byte>(bytes).subspan(12)),k);
  (void)DecodeKnownTables(bytes,k);
  return bytes;
}
}
