#include "eor_havok_packfile.h"
#include <array>
#include <stdexcept>
#include <string_view>
namespace hulk::eor::havok {
namespace {
constexpr std::array<uint8_t,8> kMagic{0x57,0xe0,0xe0,0x57,0x10,0xc0,0xc0,0x10};
constexpr std::string_view kVersion{"Havok-5.1.0-r1"};
uint8_t U8(std::span<const std::byte> bytes,size_t p) {
  return std::to_integer<uint8_t>(bytes[p]);
}
uint32_t U32(std::span<const std::byte> bytes,size_t p,Platform plat) {
  uint32_t a=U8(bytes,p), b=U8(bytes,p+1), c=U8(bytes,p+2), d=U8(bytes,p+3);
  return plat==Platform::PC ? a|(b<<8)|(c<<16)|(d<<24) : (a<<24)|(b<<16)|(c<<8)|d;
}
} // namespace
Packfile Inspect(std::span<const std::byte> bytes,uint32_t key,Platform plat) {
  if(bytes.size()<96) throw std::invalid_argument("Havok envelope too short");
  size_t marker=bytes.size();
  for(size_t i=0;i+96<=bytes.size()&&i<256;++i) {
    bool match=true;
    for(size_t j=0;j<kMagic.size();++j)
      if(U8(bytes,i+j)!=kMagic[j]) {match=false;break;}
    if(match) {marker=i;break;}
  }
  if(marker==bytes.size()) throw std::invalid_argument("Havok magic absent in EoR resource");
  if(U32(bytes,marker+8,plat)!=key || U32(bytes,marker+12,plat)!=5 ||
     U8(bytes,marker+16)!=4 || U8(bytes,marker+17)!=(plat==Platform::PC?1:0) ||
     U8(bytes,marker+18)!=0 || U8(bytes,marker+19)!=1 ||
     U32(bytes,marker+20,plat)!=3)
    throw std::invalid_argument("Havok endian/layout/userTag/sections mismatch");
  bool version=false;
  for(size_t pos=marker+36;pos+kVersion.size()<bytes.size()&&pos<marker+80;++pos) {
    bool matches=true;
    for(size_t i=0;i<kVersion.size();++i)
      if(U8(bytes,pos+i)!=static_cast<uint8_t>(kVersion[i])) {matches=false;break;}
    if(matches && U8(bytes,pos+kVersion.size())==0) {version=true;break;}
  }
  if(!version) throw std::invalid_argument("Unsupported Havok packfile version");
  // Havok 5.1 packfiles have three native sections. Their fixup arrays
  // cannot be treated as PC pointers or MOPP programs without conversion.
  const std::string_view names[]{"__classnames__", "__data__", "__types__"};
  std::array<Section,3> table{};
  for(size_t j=0;j<3;++j) {
    const size_t p=marker+64+48*j;
    if(p+48>bytes.size()) throw std::invalid_argument("Truncated Havok section table");
    for(size_t k=0;k<20;++k) {
      const uint8_t expected=k<names[j].size()?static_cast<uint8_t>(names[j][k]):0;
      if(U8(bytes,p+k)!=expected)
        throw std::invalid_argument("Unexpected Havok section name");
    }
    auto& section=table[j];
    section.absolute_start=U32(bytes,p+20,plat);
    section.local_fixups=U32(bytes,p+24,plat);
    section.global_fixups=U32(bytes,p+28,plat);
    section.virtual_fixups=U32(bytes,p+32,plat);
    section.exports=U32(bytes,p+36,plat);
    section.imports=U32(bytes,p+40,plat);
    section.end=U32(bytes,p+44,plat);
    if(section.absolute_start<208 ||
       section.absolute_start>bytes.size()-marker ||
       section.end>bytes.size()-marker-section.absolute_start ||
       section.local_fixups>section.global_fixups ||
       section.global_fixups>section.virtual_fixups ||
       section.virtual_fixups>section.exports ||
       section.exports>section.imports ||
       section.imports>section.end)
      throw std::invalid_argument("Invalid Havok fixup/section offsets");
    if(j && table[j-1].absolute_start+table[j-1].end>section.absolute_start)
      throw std::invalid_argument("Overlapping Havok sections");
  }
  return {key,5,3,marker,bytes.size(),plat,table};
}
std::vector<Fixup> DecodeFixups(std::span<const std::byte> bytes,
                                uint32_t hash, Platform plat) {
  const Packfile pack=Inspect(bytes,hash,plat);
  constexpr size_t kMaxEdges=262144;
  std::vector<Fixup> result;
  for(uint32_t section=0;section<3;++section) {
    const Section& s=pack.section_table[section];
    const size_t base=pack.header_offset+s.absolute_start;
    struct Region {uint32_t from,to,step;FixupKind kind;};
    const Region regions[]{
      {s.local_fixups,s.global_fixups,8,FixupKind::Local},
      {s.global_fixups,s.virtual_fixups,12,FixupKind::Global},
      {s.virtual_fixups,s.exports,12,FixupKind::VirtualClass}
    };
    for(const Region& region:regions) {
      for(uint32_t i=region.from;i<region.to;) {
        const size_t remaining=static_cast<size_t>(region.to)-i;
        const size_t p=base+i;
        if(remaining<region.step) {
          for(size_t k=0;k<remaining;++k)
            if(U8(bytes,p+k)!=0xff)
              throw std::invalid_argument("Malformed Havok fixup array padding");
          break;
        }
        const uint32_t from=U32(bytes,p,plat);
        const uint32_t a=U32(bytes,p+4,plat);
        const uint32_t b=region.step==12?U32(bytes,p+8,plat):0;
        i+=region.step;
        if(from==0xffffffffu) {
          if(a!=0xffffffffu || (region.step==12 && b!=0xffffffffu))
            throw std::invalid_argument("Incomplete Havok fixup sentinel");
          continue;
        }
        const uint32_t dst_section=region.step==8?section:a;
        const uint32_t dst_offset=region.step==8?a:b;
        if(from>s.local_fixups || s.local_fixups-from<4 ||
           dst_section>=3 || dst_offset>=pack.section_table[dst_section].local_fixups)
          throw std::invalid_argument("Havok fixup references invalid serialized data");
        if(result.size()>=kMaxEdges)
          throw std::invalid_argument("Havok fixup edge limit exceeded");
        result.push_back({region.kind,section,from,dst_section,dst_offset});
      }
    }
  }
  return result;
}

Packfile ReadIndexed(const Index& index,const std::filesystem::path& archive,
                     std::string_view category,uint32_t hash,size_t maximum_size) {
  if(category!="Havoks"&&category!="HavokMopps")
    throw std::invalid_argument("Unknown original EoR Havok resource group");
  const auto record=index.FindHash(category,hash);
  if(!record || record->reserved || !record->size)
    throw std::invalid_argument("Original EoR Havok resource not indexed");
  const Platform plat=index.byte_order()==ByteOrder::Big?Platform::Xbox360:Platform::PC;
  return Inspect(ReadResource(archive,*record,maximum_size),hash,plat);
}
} // namespace hulk::eor::havok
