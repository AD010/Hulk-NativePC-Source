#include "eor_original_texture_streams.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
namespace hulk::eor::textures {
namespace {
constexpr size_t kNameLimit=128;
constexpr size_t kMaxStreamSize=64u<<20;
uint32_t ReadBig32(std::span<const std::byte> bytes,size_t off) {
  if(off>bytes.size() || bytes.size()-off<4)
    throw std::invalid_argument("Truncated original Xbox LFXT stream reference");
  return (uint32_t(std::to_integer<uint8_t>(bytes[off]))<<24) |
         (uint32_t(std::to_integer<uint8_t>(bytes[off+1]))<<16) |
         (uint32_t(std::to_integer<uint8_t>(bytes[off+2]))<<8) |
          uint32_t(std::to_integer<uint8_t>(bytes[off+3]));
}
}
std::optional<OriginalStreamLink> FindVerifiedOriginalTexStream(
    std::span<const std::byte> raw, uint32_t texture_hash, const Index& index) {
  if(index.byte_order()!=ByteOrder::Big ||
     !index.groups().contains("Textures") ||
     !index.groups().contains("TexStreams"))
    throw std::invalid_argument("Missing matching Xbox EoR Textures/TexStreams index");
  if(!index.FindHash("Textures",texture_hash))
    throw std::invalid_argument("Original LFXT identity absent from Xbox index");
  if(raw.size()<8 || raw.size()>64u*1024u*1024u ||
     raw[0]!=std::byte{'L'} || raw[1]!=std::byte{'F'} ||
     raw[2]!=std::byte{'X'} || raw[3]!=std::byte{'T'})
    throw std::invalid_argument("Not an original Xbox LFXT texture resource");
  const size_t limit=std::min(raw.size(), kNameLimit+5);
  size_t end=4;
  while(end<limit && raw[end]!=std::byte{0}) {
    const auto c=std::to_integer<unsigned char>(raw[end]);
    if(c<0x21 || c>0x7e || c=='/' || c=='\\')
      throw std::invalid_argument("Non-ASCII original LFXT resource name");
    ++end;
  }
  if(end==4 || end==limit)
    throw std::invalid_argument("Invalid original Xbox LFXT resource name termination");
  const auto* name_data=reinterpret_cast<const char*>(raw.data()+4);
  const std::string_view name{name_data,end-4};
  if(ResourceHash(name)!=texture_hash)
    throw std::invalid_argument("Original Xbox LFXT name/hash mismatch");
  std::string stream_name{name};
  stream_name+="_STREAM";
  if(stream_name.size()>128) return std::nullopt;
  const uint32_t stream_hash=ResourceHash(stream_name);
  const auto stream=index.FindHash("TexStreams",stream_hash);
  if(!stream)return std::nullopt;
  if(stream->hash!=stream_hash || stream->reserved || !stream->size ||
     stream->size>kMaxStreamSize || stream->offset<4)
    throw std::invalid_argument("Unsafe indexed Xbox TexStreams payload");
  const size_t base=end+1;
  bool found=false;
  size_t matching=0;
  for(size_t relative: {size_t(34),size_t(22)}) {
    const size_t offset=base+relative;
    if(offset>raw.size() || raw.size()-offset<4) continue;
    if(ReadBig32(raw,offset)!=stream_hash) continue;
    if(found)throw std::invalid_argument("Ambiguous original Xbox LFXT stream pointer");
    found=true;matching=offset;
  }
  if(!found)
    throw std::invalid_argument("Indexed Xbox texture stream hash absent from original LFXT header");
  return OriginalStreamLink{texture_hash,stream_hash,*stream,matching};
}
std::vector<std::byte> ReadOriginalTexStream(
    const std::filesystem::path& archive,const OriginalStreamLink& link) {
  if(archive.empty() || !link.original_texture_hash || !link.original_stream_hash ||
     link.indexed_stream.hash!=link.original_stream_hash ||
     link.indexed_stream.reserved || !link.indexed_stream.size ||
     link.indexed_stream.size>kMaxStreamSize)
    throw std::invalid_argument("Unverified source for original Xbox high-resolution texture stream");
  return ReadResource(archive,link.indexed_stream,kMaxStreamSize);
}
} // namespace hulk::eor::textures
