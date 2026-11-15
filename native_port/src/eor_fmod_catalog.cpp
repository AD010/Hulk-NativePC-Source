#include "eor_fmod_catalog.h"
#include <fstream>
#include <stdexcept>
#include <utility>
namespace hulk::eor::fmod {
namespace {
uint8_t Byte(std::span<const std::byte> b,size_t p) {
  return std::to_integer<uint8_t>(b[p]);
}
uint32_t U32(std::span<const std::byte> b,size_t p) {
  if(p>b.size() || b.size()-p<4)
    throw std::invalid_argument("FMOD catalog truncated");
  return uint32_t(Byte(b,p))<<24 | uint32_t(Byte(b,p+1))<<16 |
         uint32_t(Byte(b,p+2))<<8 | Byte(b,p+3);
}
}
std::vector<CatalogEntry> DecodeCatalog(std::span<const std::byte> archive,
                                        std::span<const Resource> records) {
  if(records.empty() || records.size()>65536 || archive.size()>(64u<<20))
    throw std::invalid_argument("Unbounded FMOD catalog");
  size_t previous=4;
  for(const auto& record:records) {
    if(record.reserved || record.size<20 || record.offset<previous ||
       uint64_t(record.offset)+record.size>archive.size())
      throw std::invalid_argument("FMOD catalog invalid index range");
    previous=uint64_t(record.offset)+record.size;
  }
  size_t pos=previous;
  if(U32(archive,pos)!=records.size())
    throw std::invalid_argument("FMOD catalog count disagrees with owner index");
  pos+=4;
  std::vector<CatalogEntry> entries;
  entries.reserve(records.size());
  for(const auto& indexed:records) {
    const uint32_t hash=U32(archive,pos);
    const uint32_t offset=U32(archive,pos+4);
    const uint32_t size=U32(archive,pos+8);
    pos+=12;
    if(hash!=indexed.hash || offset!=indexed.offset || size!=indexed.size)
      throw std::invalid_argument("FMOD catalog entry disagrees with owner index");
    std::string name;
    for(;pos<archive.size() && Byte(archive,pos)!=0;++pos) {
      const uint8_t ch=Byte(archive,pos);
      if(ch<0x21 || ch>0x7e || ch=='/' || ch=='\\' || name.size()>=128)
        throw std::invalid_argument("Invalid FMOD event name");
      name.push_back(static_cast<char>(ch));
    }
    if(name.empty() || pos>=archive.size())
      throw std::invalid_argument("Unterminated FMOD event name");
    ++pos;
    if(ResourceHash(name)!=hash)
      throw std::invalid_argument("FMOD event CRC32 mismatched owner index");
    const std::array<uint32_t,3> opaque{
      U32(archive,pos),U32(archive,pos+4),U32(archive,pos+8)};
    pos+=12;
    entries.push_back({indexed,std::move(name),opaque});
  }
  if(pos+4!=archive.size() || U32(archive,pos)!=0)
    throw std::invalid_argument("Unexpected FMOD catalog footer or trailing bytes");
  return entries;
}
std::vector<CatalogEntry> ReadCatalog(const Index& index,
                                     const std::filesystem::path& path) {
  if(index.byte_order()!=ByteOrder::Big)
    throw std::invalid_argument("FMOD catalog needs original big-endian Xbox index");
  const auto it=index.groups().find("FmodProjects");
  if(it==index.groups().end())
    throw std::invalid_argument("FmodProjects index group missing");
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  if(!file)throw std::runtime_error("Cannot open original FMOD archive");
  const auto size=file.tellg();
  if(size<0 || size>(64<<20))
    throw std::invalid_argument("Unbounded FMOD archive");
  std::vector<std::byte> bytes(static_cast<size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size));
  if(!file)throw std::runtime_error("Cannot read complete FMOD archive");
  return DecodeCatalog(bytes,it->second);
}
}
