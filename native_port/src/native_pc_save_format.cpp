#include "native_pc_save_format.h"
#include <fstream>
#include <stdexcept>
#include <vector>
namespace hulk::save::format {
namespace {
uint32_t U32(std::span<const std::byte> b,size_t p,Kind kind) {
  if(p>b.size() || b.size()-p<4)
    throw std::invalid_argument("Hulk save word truncated");
  const uint32_t a=std::to_integer<uint8_t>(b[p]),
      c=std::to_integer<uint8_t>(b[p+1]),
      d=std::to_integer<uint8_t>(b[p+2]),
      e=std::to_integer<uint8_t>(b[p+3]);
  return kind==Kind::XboxBigEndian
    ? (a<<24)|(c<<16)|(d<<8)|e : a|(c<<8)|(d<<16)|(e<<24);
}
uint32_t Crc(std::span<const std::byte> b) {
  uint32_t crc=0xffffffffu;
  for(const auto x:b) {
    crc^=std::to_integer<uint8_t>(x);
    for(unsigned i=0;i<8;++i)
      crc=(crc>>1)^((crc&1u)?0xedb88320u:0);
  }
  return crc^0xffffffffu;
}
}
Summary Inspect(std::span<const std::byte> bytes,Kind kind) {
  if(bytes.size()<256 || bytes.size()>(16u<<20))
    throw std::invalid_argument("Invalid Hulk save length");
  const uint32_t crc=U32(bytes,0,kind);
  if(crc!=Crc(bytes.subspan(12)))
    throw std::invalid_argument("Hulk save CRC mismatch");
  const uint32_t length=U32(bytes,4,kind);
  if(!length || length>bytes.size())
    throw std::invalid_argument("Hulk save length header invalid");
  const size_t start=kind==Kind::XboxBigEndian?20:16;
  uint32_t count=0,zero=0,previous=0;
  for(size_t pos=start;pos+8<=bytes.size()&&count<4096;pos+=8) {
    const uint32_t key=U32(bytes,pos,kind);
    if(key<=previous)break;
    previous=key;
    ++count;
    zero+=U32(bytes,pos+4,kind)==0;
  }
  if(count<16 || count>1024)
    throw std::invalid_argument("Hulk save field table invalid");
  return {kind,crc,length,count,zero,bytes.size(),false};
}
Summary Read(const std::filesystem::path& path,Kind kind,size_t maximum) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  if(!file)throw std::runtime_error("Cannot open user Hulk save");
  const auto length=file.tellg();
  if(length<0 || static_cast<uint64_t>(length)>maximum ||
     maximum>(16u<<20))
    throw std::invalid_argument("Hulk save outside private safety bound");
  std::vector<std::byte> bytes(static_cast<size_t>(length));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(length));
  if(!file)throw std::runtime_error("Cannot read complete Hulk save");
  return Inspect(bytes,kind);
}
}
