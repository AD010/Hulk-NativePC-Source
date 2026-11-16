#include "eor_original_texture_streams.h"
#include <algorithm>
#include <bit>
#include <cmath>
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
std::optional<OriginalLfxtInlineSection> FindOriginalLfxtInlineSection(
    std::span<const std::byte> bytes, uint32_t original_hash) {
  if(bytes.size()<20 || bytes.size()>kMaxStreamSize ||
     bytes[0]!=std::byte{'L'} || bytes[1]!=std::byte{'F'} ||
     bytes[2]!=std::byte{'X'} || bytes[3]!=std::byte{'T'})
    return std::nullopt;
  const size_t name_limit=std::min(bytes.size(), kNameLimit+5);
  size_t name_end=4;
  while(name_end<name_limit && bytes[name_end]!=std::byte{0}) {
    const auto value=std::to_integer<unsigned char>(bytes[name_end]);
    if(value<0x21 || value>0x7e || value=='/' || value=='\\')
      throw std::invalid_argument("Malformed USA LFXT name in original texture");
    ++name_end;
  }
  if(name_end==4 || name_end==name_limit)
    throw std::invalid_argument("Unterminated original USA LFXT name");
  const std::string_view name{
      reinterpret_cast<const char*>(bytes.data()+4),name_end-4};
  if(!original_hash || ResourceHash(name)!=original_hash)
    throw std::invalid_argument("USA original LFXT resource name/hash mismatch");
  const size_t start=name_end+1;
  const size_t end=std::min(bytes.size()-8,start+128);
  std::optional<OriginalLfxtInlineSection> matched;
  for(size_t at=start;at<=end;++at) {
    const uint64_t outer=ReadBig32(bytes,at);
    const uint64_t inner=ReadBig32(bytes,at+4);
    // Each length field declares all remaining bytes after itself:
    // 0x8038 / 0x8034, 0x4038 / 0x4034, ... from genuine USA assets.
    if(outer!=bytes.size()-at-4 ||
       inner!=bytes.size()-at-8 ||
       inner<52)
      continue;
    if(matched)
      throw std::invalid_argument("Ambiguous original EoR LFXT data suffix");
    matched=OriginalLfxtInlineSection{
        at,at+4,at+8,static_cast<size_t>(inner)};
  }
  return matched;
}
std::optional<OriginalLfxtStreamTable> ParseOriginalLfxtStreamTable(
    std::span<const std::byte> bytes, const OriginalStreamLink& link) {
  if(!link.original_texture_hash || !link.original_stream_hash ||
     link.indexed_stream.hash!=link.original_stream_hash ||
     link.indexed_stream.reserved ||
     link.indexed_stream.size<56 ||
     link.indexed_stream.size>kMaxStreamSize)
    throw std::invalid_argument("Unverified original LFXT stream owner/index");
  const auto inline_section=FindOriginalLfxtInlineSection(
      bytes,link.original_texture_hash);
  if(!inline_section)return std::nullopt;
  if(link.original_stream_hash_offset>bytes.size() ||
     bytes.size()-link.original_stream_hash_offset<13)
    return std::nullopt;
  if(ReadBig32(bytes,link.original_stream_hash_offset)!=link.original_stream_hash)
    throw std::invalid_argument("Original USA LFXT stream hash differs from IDX");
  const size_t count_offset=link.original_stream_hash_offset+8;
  const size_t entry_offset=count_offset+1;
  const size_t count=std::to_integer<unsigned char>(bytes[count_offset]);
  if(!count || count>14 ||
     entry_offset+count*8!=inline_section->outer_length_field_offset)
    return std::nullopt;
  OriginalLfxtStreamTable result;
  result.original_count_offset=count_offset;
  result.original_entries_offset=entry_offset;
  result.levels.reserve(count);
  for(size_t i=0;i<count;++i) {
    const size_t at=entry_offset+8*i;
    const uint32_t size=ReadBig32(bytes,at);
    const float metric=std::bit_cast<float>(ReadBig32(bytes,at+4));
    if(!size || size%4096 || !std::isfinite(metric) ||
       metric<=0.0f || metric>1000000.0f)
      return std::nullopt;
    result.summed_source_bytes+=size;
    if(result.summed_source_bytes>kMaxStreamSize)
      return std::nullopt;
    result.levels.push_back({size,metric});
  }
  if(result.summed_source_bytes != uint64_t(link.indexed_stream.size)-56)
    return std::nullopt;
  return result;
}
std::optional<OriginalLfxtMipPlan> ReconstructOriginalLfxtMipPlan(
    std::span<const std::byte> bytes,
    uint32_t texture_hash,
    const OriginalLfxtInlineSection& inline_section,
    const OriginalLfxtStreamTable& streamed) {
  const auto original_section=FindOriginalLfxtInlineSection(bytes,texture_hash);
  if(!original_section ||
     original_section->outer_length_field_offset !=
         inline_section.outer_length_field_offset ||
     original_section->inner_length_field_offset !=
         inline_section.inner_length_field_offset ||
     original_section->inline_data_offset!=inline_section.inline_data_offset ||
     original_section->inline_data_bytes!=inline_section.inline_data_bytes ||
     inline_section.inline_data_bytes<=52 ||
     streamed.levels.empty() || streamed.levels.size()>14)
    return std::nullopt;
  size_t end=4;
  while(end<bytes.size() && bytes[end]!=std::byte{0}) ++end;
  if(end>=bytes.size())return std::nullopt;
  const size_t base=end+1;
  if(base>bytes.size() || bytes.size()-base<9)
    return std::nullopt;
  // Original EoR LFXT 16-bit BE resident mip width/height:
  // (NUL+1)+3 and (NUL+1)+7, including non-square 64x32, 128x32 etc.
  const uint32_t w=(uint32_t(std::to_integer<uint8_t>(bytes[base+3]))<<8) |
                    uint32_t(std::to_integer<uint8_t>(bytes[base+4]));
  const uint32_t h=(uint32_t(std::to_integer<uint8_t>(bytes[base+7]))<<8) |
                    uint32_t(std::to_integer<uint8_t>(bytes[base+8]));
  if(!w || !h || w>8192 || h>8192)
    return std::nullopt;
  // A 32x32 Xenos tiled macroblock uses actual TEXEL BLOCKS for BCn,
  // and individual texels for uncompressed formats.
  struct Shape {OriginalLfxtStorageFamily family;uint32_t pixels,bytes;};
  constexpr Shape candidate[]{
    {OriginalLfxtStorageFamily::BC_8ByteBlock,4,8},
    {OriginalLfxtStorageFamily::BC_16ByteBlock,4,16},
    {OriginalLfxtStorageFamily::Texel_8Byte,1,8}
  };
  const auto padded=[&](uint32_t width,uint32_t height,
                         const Shape& shape)->uint64_t {
    const uint64_t bw=(uint64_t(width)+shape.pixels-1)/shape.pixels;
    const uint64_t bh=(uint64_t(height)+shape.pixels-1)/shape.pixels;
    const uint64_t pb=(bw+31u)&~uint64_t(31);
    const uint64_t ph=(bh+31u)&~uint64_t(31);
    return pb*ph*shape.bytes;
  };
  std::optional<OriginalLfxtMipPlan> accepted;
  for(const auto& shape:candidate) {
    OriginalLfxtMipPlan plan;
    plan.family=shape.family;
    plan.resident_width=w;plan.resident_height=h;
    bool valid=true;
    const size_t count=streamed.levels.size();
    uint64_t accounted=0;
    for(size_t i=0;i<count;++i) {
      const uint64_t factor=uint64_t{1}<<(count-i);
      const uint64_t width=uint64_t(w)*factor;
      const uint64_t height=uint64_t(h)*factor;
      if(width>8192 || height>8192) {valid=false;break;}
      const uint64_t bytes_required=padded(
          static_cast<uint32_t>(width),static_cast<uint32_t>(height),shape);
      const auto& actual=streamed.levels[i];
      const double metric=std::sqrt(static_cast<double>(width)*height);
      if(bytes_required!=actual.source_aligned_bytes ||
         std::fabs(static_cast<double>(actual.original_level_metric)-metric)>
               std::max(0.01,metric*0.00002)) {
        valid=false;break;
      }
      accounted+=bytes_required;
      plan.levels.push_back({
          static_cast<uint32_t>(width),static_cast<uint32_t>(height),
          static_cast<uint32_t>(bytes_required),true});
    }
    if(!valid || accounted!=streamed.summed_source_bytes)
      continue;
    const uint64_t inline_payload=inline_section.inline_data_bytes-52;
    uint64_t total=0;
    uint32_t width=w,height=h;
    for(unsigned mip=0;mip<14;++mip) {
      const uint64_t size=padded(width,height,shape);
      total+=size;
      if(total>inline_payload)break;
      plan.levels.push_back({
          width,height,static_cast<uint32_t>(size),false});
      if(total==inline_payload)break;
      if(width==1 && height==1)break;
      width=std::max(1u,width>>1);height=std::max(1u,height>>1);
    }
    if(total!=inline_payload)
      continue;
    if(accepted) // More than one format family fits: fail closed.
      return std::nullopt;
    accepted=std::move(plan);
  }
  return accepted;
}
std::span<const std::byte> ValidateOriginalTexStreamEnvelope(
    std::span<const std::byte> bytes) {
  if (bytes.size() < 5 || bytes.size() > kMaxStreamSize)
    throw std::invalid_argument("Original Xbox TexStreams record size is unsafe");
  const uint32_t declared = ReadBig32(bytes, 0);
  if (declared != bytes.size() - 4)
    throw std::invalid_argument("Original Xbox TexStreams BE length-prefix mismatch");
  return bytes.subspan(4);
}
std::span<const std::byte> VerifiedOriginalStreamByteRange(
    std::span<const std::byte> bytes, uint64_t offset, uint64_t count) {
  (void)ValidateOriginalTexStreamEnvelope(bytes);
  if (offset < 4 || count == 0 ||
      offset > bytes.size() || count > bytes.size() - offset)
    throw std::invalid_argument("Original Xbox TexStreams GPU bytes outside BE-framed record");
  return bytes.subspan(static_cast<size_t>(offset), static_cast<size_t>(count));
}
std::vector<std::byte> ReadOriginalTexStream(
    const std::filesystem::path& archive,const OriginalStreamLink& link) {
  if(archive.empty() || !link.original_texture_hash || !link.original_stream_hash ||
     link.indexed_stream.hash!=link.original_stream_hash ||
     link.indexed_stream.reserved || !link.indexed_stream.size ||
     link.indexed_stream.size>kMaxStreamSize)
    throw std::invalid_argument("Unverified source for original Xbox high-resolution texture stream");
  auto record=ReadResource(archive,link.indexed_stream,kMaxStreamSize);
  (void)ValidateOriginalTexStreamEnvelope(record);
  // Keep full original record intact for source-relative mip byte offsets.
  // Only verified format/tiling metadata may decide how to read GPU bytes.
  return record;
}
} // namespace hulk::eor::textures
