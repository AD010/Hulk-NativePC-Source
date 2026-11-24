#include "eor_original_virtual_shader_identity.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <set>
#include <cstring>

namespace hulk::eor::aot {
namespace {
constexpr std::array<uint32_t,64> kShaConstants{
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
  0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
  0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
  0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
  0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
  0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
  0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
  0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
  0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};
constexpr uint32_t Rotate(uint32_t v,unsigned n) noexcept {
  return (v>>n)|(v<<(32-n));
}
uint32_t Be32(const uint8_t* data) noexcept {
  return (uint32_t(data[0])<<24)|(uint32_t(data[1])<<16)|
         (uint32_t(data[2])<<8)|uint32_t(data[3]);
}
uint32_t Le32(const uint8_t* data) noexcept {
  return uint32_t(data[0])|(uint32_t(data[1])<<8)|
         (uint32_t(data[2])<<16)|(uint32_t(data[3])<<24);
}
void Transform(std::array<uint32_t,8>& state,const uint8_t* block) noexcept {
  std::array<uint32_t,64> words{};
  for(unsigned i=0;i<16;++i)words[i]=Be32(block+4*i);
  for(unsigned i=16;i<64;++i) {
    const auto x=words[i-15],y=words[i-2];
    const auto s0=Rotate(x,7)^Rotate(x,18)^(x>>3);
    const auto s1=Rotate(y,17)^Rotate(y,19)^(y>>10);
    words[i]=words[i-16]+s0+words[i-7]+s1;
  }
  auto [a,b,c,d,e,f,g,h]=state;
  for(unsigned i=0;i<64;++i) {
    const auto choice=(e&f)^(~e&g);
    const auto majority=(a&b)^(a&c)^(b&c);
    const auto t1=h+(Rotate(e,6)^Rotate(e,11)^Rotate(e,25))+
                  choice+kShaConstants[i]+words[i];
    const auto t2=(Rotate(a,2)^Rotate(a,13)^Rotate(a,22))+majority;
    h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
  }
  state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
  state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}
} // namespace

Digest SourceSha256(std::span<const uint8_t> bytes) noexcept {
  std::array<uint32_t,8> state{
      0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
      0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
  size_t index=0;
  while(bytes.size()-index>=64) {
    Transform(state,bytes.data()+index);
    index+=64;
  }
  std::array<uint8_t,128> tail{};
  const size_t remain=bytes.size()-index;
  for(size_t j=0;j<remain;++j)tail[j]=bytes[index+j];
  tail[remain]=0x80;
  const size_t total=remain+1+8<=64 ? 64 : 128;
  const uint64_t bitlength=static_cast<uint64_t>(bytes.size())*8u;
  for(size_t j=0;j<8;++j)
    tail[total-8+j]=uint8_t(bitlength>>(8*(7-j)));
  Transform(state,tail.data());
  if(total==128)Transform(state,tail.data()+64);
  Digest out{};
  for(size_t i=0;i<8;++i)
    for(size_t j=0;j<4;++j)
      out[i*4+j]=uint8_t(state[i]>>(8*(3-j)));
  return out;
}

namespace {
// Cache only the mathematical digest of byte-identical data. Never cache
// a guest address, stage, bank entry or authorization. Re-read the entire
// live header on each Draw; a changed byte always goes through SHA-256.
// Thread-local bounded storage also avoids locking the PPC render thread.
Digest ExactHeaderDigest(std::span<const uint8_t> bytes) noexcept {
  constexpr size_t kSlots=512, kMaxCachedBytes=4096;
  if(bytes.size()>kMaxCachedBytes)return OriginalShaderVirtualSha256(bytes);
  struct Entry {std::vector<uint8_t> bytes; Digest digest{};};
  static thread_local std::array<Entry,kSlots> cache;
  // Fast non-cryptographic slot selection only. The exact byte comparison
  // below is mandatory, so a collision cannot admit a different program.
  uint64_t key=0xcbf29ce484222325ull;
  size_t i=0;
  for(;bytes.size()-i>=8;i+=8) {
    uint64_t word=0;std::memcpy(&word,bytes.data()+i,8);
    key=(key^word)*0x100000001b3ull;
  }
  for(;i<bytes.size();++i)key=(key^bytes[i])*0x100000001b3ull;
  auto& entry=cache[static_cast<size_t>((key^(key>>32))&(kSlots-1))];
  if(entry.bytes.size()==bytes.size() &&
     std::equal(bytes.begin(),bytes.end(),entry.bytes.begin()))
    return entry.digest;
  const auto digest=OriginalShaderVirtualSha256(bytes);
  try {
    entry.bytes.assign(bytes.begin(),bytes.end());
    entry.digest=digest;
  } catch (...) {entry.bytes.clear();}
  return digest;
}
} // namespace

void OriginalVirtualShaderIdentity::Load(
    const std::filesystem::path& private_bank) {
  std::ifstream file(private_bank,std::ios::binary|std::ios::ate);
  if(!file)throw std::invalid_argument("Original private virtual shader index absent");
  const auto size=file.tellg();
  if(size<16 || size>16ll+72ll*16384)
    throw std::invalid_argument("Original virtual shader index too large");
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  file.seekg(0);
  if(!file.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))
    throw std::invalid_argument("Original virtual shader bank truncated");
  if(std::string(bytes.begin(),bytes.begin()+8)!="HEORVRT1")
    throw std::invalid_argument("Unknown source virtual-shader identity bank");
  const auto count=Le32(bytes.data()+8);
  const auto version=Le32(bytes.data()+12);
  if(version!=1 || count==0 || count>16384 ||
     bytes.size()!=16ull+72ull*count)
    throw std::invalid_argument("Original virtual-shader source record count invalid");
  std::map<Digest,Entry> next;
  std::set<Digest> unique_original_sources;
  for(size_t i=0;i<count;++i) {
    const auto base=16ull+i*72ull;
    Digest virtual_sha{},full_sha{};
    for(size_t b=0;b<32;++b) {
      virtual_sha[b]=bytes[base+b];
      full_sha[b]=bytes[base+32+b];
    }
    const auto stage=Le32(bytes.data()+base+64);
    const auto virtual_size=Le32(bytes.data()+base+68);
    if(virtual_sha==Digest{} || full_sha==Digest{} ||
       (stage!=1u && stage!=2u) ||
       virtual_size<36u || virtual_size>(8u<<20) ||
       !unique_original_sources.insert(full_sha).second ||
       !next.emplace(virtual_sha,
                     Entry{full_sha,static_cast<Stage>(stage),virtual_size}).second)
      throw std::invalid_argument("Original source virtual SHA identity collision");
  }
  original_.swap(next); // Failed generation leaves older verified bank unchanged.
  pixel_debug_aliases_.clear();pixel_metadata_aliases_.clear();
  pixel_alias_count_=pixel_metadata_count_=0;
}

std::optional<Digest> OriginalVirtualShaderIdentity::FindOriginalSource(
    std::span<const uint8_t> exact_header,Stage expected_stage) const noexcept {
  if((expected_stage!=Stage::Vertex && expected_stage!=Stage::Pixel) ||
     exact_header.size()<36 || exact_header.size()>(8u<<20))
    return std::nullopt;
  const uint32_t flags=Be32(exact_header.data());
  if((flags&0xffffff00u)!=0x102a1100u ||
     ((flags&1u)?Stage::Vertex:Stage::Pixel)!=expected_stage ||
     Be32(exact_header.data()+4)!=exact_header.size() ||
     Be32(exact_header.data()+8)==0u)
    return std::nullopt;
  const auto virtual_sha=ExactHeaderDigest(exact_header);
  const auto it=original_.find(virtual_sha);
  if(it==original_.end() || it->second.stage!=expected_stage ||
     it->second.virtual_bytes!=exact_header.size())
    return std::nullopt;
  return it->second.full_source_sha;
}
std::optional<Digest> OriginalPixelDebugHeaderKey(
    std::span<const uint8_t> header) noexcept {
  // All 2,597 original USA pixel containers have this exact UPDB record
  // layout: +12 -> debug record, +16 -> next section; record flags and
  // filename length are BE32, followed by the filename and zero padding.
  // Only filename payload bytes may differ. Offsets, length, padding,
  // CTAB, instruction descriptors and all other fields remain significant.
  if(header.size()<44 || header.size()>(1u<<20) ||
     Be32(header.data())!=0x102a1100u ||
     Be32(header.data()+4)!=header.size() || !Be32(header.data()+8) ||
     Be32(header.data()+12)!=36u || Be32(header.data()+36)!=0u)return {};
  const auto next=Be32(header.data()+16),length=Be32(header.data()+40);
  if(next>header.size() || next<44 || !length || length>next-44u ||
     next-44u-length>3u)return {};
  const auto filename=header.subspan(44,length);
  const auto padding=header.subspan(44u+length,next-44u-length);
  if(!std::all_of(padding.begin(),padding.end(),[](uint8_t x){return x==0;}))return {};
  const bool cleared=std::all_of(filename.begin(),filename.end(),[](uint8_t x){return x==0;});
  if(!cleared) {
    if(length<5u || !std::all_of(filename.begin(),filename.end(),
                    [](uint8_t x){return x>=32u && x<127u;}))return {};
    constexpr char ext[]=".updb";
    for(size_t i=0;i<5;++i) {
      auto c=filename[length-5+i];
      if(c>='A' && c<='Z')c=uint8_t(c+32u);
      if(c!=uint8_t(ext[i]))return {};
    }
  }
  try {
    std::vector<uint8_t> canonical(header.begin(),header.end());
    std::fill_n(canonical.begin()+44,length,uint8_t{0});
    return ExactHeaderDigest(canonical);
  } catch (...) {return {};}
}
std::optional<Digest> OriginalPixelMetadataKey(
    std::span<const uint8_t> header) noexcept {
  // The pinned 2597 PS containers use this exact 36-byte directory.
  // +4 virtual size, +12 UPDB, +16 CTAB, +20 optional definitions,
  // +24 instruction descriptor; +28/+32 are zero in EVERY source.
  // The CTAB and everything following it must remain byte-identical.
  if(header.size()<36 || header.size()>(1u<<20) ||
     Be32(header.data())!=0x102a1100u ||
     Be32(header.data()+4)!=header.size() || !Be32(header.data()+8) ||
     Be32(header.data()+28) || Be32(header.data()+32))return {};
  const auto debug=Be32(header.data()+12),ctab=Be32(header.data()+16);
  const auto defs=Be32(header.data()+20),desc=Be32(header.data()+24);
  if(ctab<36u || ctab>header.size() || desc<ctab ||
     uint64_t(desc)+24u>header.size() ||
     (defs && (defs<ctab || defs>=desc)))return {};
  if(debug==0u) {
    if(ctab!=36u)return {};
  } else if(debug!=36u || !OriginalPixelDebugHeaderKey(header))return {};
  try {
    const auto removed=ctab-36u;
    std::vector<uint8_t> canonical(header.begin(),header.begin()+36);
    canonical.insert(canonical.end(),header.begin()+ctab,header.end());
    auto write=[&](size_t at,uint32_t value) {
      for(unsigned i=0;i<4;++i)canonical[at+i]=uint8_t(value>>(24-8*i));
    };
    write(4,static_cast<uint32_t>(canonical.size()));
    write(12,0u);write(16,36u);
    write(20,defs?defs-removed:0u);write(24,desc-removed);
    return ExactHeaderDigest(canonical);
  } catch (...) {return {};}
}
size_t OriginalVirtualShaderIdentity::LoadOriginalPixelDebugAliases(
    const std::filesystem::path& root) noexcept {
  if(root.empty() || original_.size()!=5194u)return 0;
  if(pixel_alias_count_)return pixel_alias_count_;
  // Original USA ShaderEffects archive SHA-256 (private owner source).
  constexpr Digest archive_sha{
      0xc3,0x35,0xe0,0xad,0x39,0xbe,0x45,0x5e,
      0x34,0xdc,0xf6,0xe0,0x2d,0xfb,0xca,0xc7,
      0x4d,0x17,0x18,0x4c,0x6a,0x39,0xce,0xb3,
      0x84,0x11,0xcf,0x9b,0xe4,0x2e,0x89,0xdd};
  try {
    for(const auto& file:{root/"DATA"/"XENON"/"SHADEREF.ARC",
        root/"XENON"/"SHADEREF.ARC",root/"SHADEREF.ARC",
        root/"DATA"/"XENON"/"USENGLISH"/"SHADEREF.ARC",
        root/"USENGLISH"/"SHADEREF.ARC"}) {
      std::ifstream f(file,std::ios::binary|std::ios::ate);
      if(!f || f.tellg()!=5913920)continue;
      std::vector<uint8_t> bytes(5913920u);
      f.seekg(0);
      if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) ||
         SourceSha256(bytes)!=archive_sha)continue;
      std::map<Digest,std::vector<PixelDebugAlias>> next,metadata;
      std::set<Digest> seen;
      for(size_t i=0;i+44<=bytes.size();++i) {
        if(Be32(bytes.data()+i)!=0x102a1100u)continue;
        const auto v=Be32(bytes.data()+i+4),p=Be32(bytes.data()+i+8);
        if(v<44 || v>(1u<<20) || !p || p>(1u<<20) ||
           uint64_t(i)+v+p>bytes.size())continue;
        const auto header=std::span<const uint8_t>(bytes).subspan(i,v);
        const auto physical=std::span<const uint8_t>(bytes).subspan(i+v,p);
        const auto source=FindOriginalSource(header,Stage::Pixel);
        if(!source || SourceSha256(std::span<const uint8_t>(bytes).subspan(i,v+p))!=*source)continue;
        const auto key=OriginalPixelDebugHeaderKey(header);
        const auto metadata_key=OriginalPixelMetadataKey(header);
        if(!key || !metadata_key || !seen.insert(*source).second)return 0;
        PixelDebugAlias entry{*source,SourceSha256(physical),
                              {header.begin(),header.end()}};
        next[*key].push_back(entry);
        metadata[*metadata_key].push_back(std::move(entry));
        i+=size_t(v)+p-1;
      }
      if(seen.size()!=2597u)return 0;
      // Remove EVERY ambiguous source; no guessed effect, path, or ordinal
      // may select among programs with the same normalized metadata/code.
      const auto unique_only=[](auto& candidates) {
        size_t admitted=0;
        for(auto it=candidates.begin();it!=candidates.end();) {
          std::map<Digest,size_t> counts;
          for(const auto& row:it->second)++counts[row.physical_sha];
          auto& entries=it->second;
          entries.erase(std::remove_if(entries.begin(),entries.end(),
             [&](const PixelDebugAlias& row){return counts.at(row.physical_sha)!=1;}),entries.end());
          admitted+=entries.size();
          if(entries.empty())it=candidates.erase(it);else ++it;
        }
        return admitted;
      };
      const auto admitted=unique_only(next),metadata_admitted=unique_only(metadata);
      if(admitted!=2595u || metadata_admitted!=1601u)return 0;
      pixel_debug_aliases_.swap(next);pixel_alias_count_=admitted;
      pixel_metadata_aliases_.swap(metadata);pixel_metadata_count_=metadata_admitted;
      return admitted;
    }
  } catch (...) {}
  return 0;
}
bool OriginalVirtualShaderIdentity::HasPixelDebugCandidate(
    std::span<const uint8_t> header) const noexcept {
  if(pixel_debug_aliases_.empty())return false;
  if(const auto key=OriginalPixelDebugHeaderKey(header);key && pixel_debug_aliases_.contains(*key))return true;
  const auto metadata=OriginalPixelMetadataKey(header);
  return metadata && pixel_metadata_aliases_.contains(*metadata);
}
const OriginalVirtualShaderIdentity::PixelDebugAlias*
OriginalVirtualShaderIdentity::FindPixelDebugAlias(
    std::span<const uint8_t> header,std::span<const uint8_t> physical) const noexcept {
  if(pixel_debug_aliases_.empty() || header.size()<36 ||
     physical.empty() || physical.size()!=Be32(header.data()+8))return nullptr;
  const auto digest=ExactHeaderDigest(physical);
  const auto find=[&](const auto& candidates,const std::optional<Digest>& key)
      ->const PixelDebugAlias* {
    if(!key)return nullptr;
    const auto it=candidates.find(*key);
    if(it==candidates.end())return nullptr;
    const PixelDebugAlias* found=nullptr;
    for(const auto& entry:it->second)if(entry.physical_sha==digest) {
      if(found)return nullptr;
      found=&entry;
    }
    return found;
  };
  if(const auto* exact=find(pixel_debug_aliases_,OriginalPixelDebugHeaderKey(header)))
    return exact;
  return find(pixel_metadata_aliases_,OriginalPixelMetadataKey(header));
}
namespace {
std::optional<uint32_t> UsaShaderHeaderAddress(uint32_t object, Stage stage) noexcept {
  if (!object || (object & 3u) || (stage != Stage::Vertex && stage != Stage::Pixel))
    return std::nullopt;
  const uint64_t address=uint64_t(object)+(stage == Stage::Vertex ? 872u : 40u);
  if (address+12u>uint64_t(UINT32_MAX)+1u) return std::nullopt;
  return static_cast<uint32_t>(address);
}
}
std::optional<OriginalLiveXdkShaderHeaderWitness>
ObserveOriginalBoundXdkShader(
    const OriginalVirtualShaderIdentity& identities, uint32_t object,
    Stage stage, ReadOriginalGuestShaderBytes reader, void* original_guest_base,
    uint64_t exact_frame_sequence, uint32_t exact_call_ordinal) noexcept {
  if (!reader || !original_guest_base || !identities.size() ||
      !exact_frame_sequence || exact_call_ordinal == UINT32_MAX)
    return std::nullopt;

  // USA shader extraction functions 0x82876EF8 (VS) and 0x82876EA0 (PS)
  // copy the virtual ShaderContainer directly from the bound XDK object.
  // Physical instructions and constants live in a separate allocation.
  const auto address = UsaShaderHeaderAddress(object,stage);
  if (!address) return std::nullopt;
  const uint32_t header_address = *address;
  try {
    std::array<uint8_t,12> preamble{};
    if(!reader(original_guest_base,header_address,
               reinterpret_cast<std::byte*>(preamble.data()),preamble.size()))
      return std::nullopt;
    const uint32_t signature=Be32(preamble.data());
    const uint32_t virtual_bytes=Be32(preamble.data()+4);
    const uint32_t physical_bytes=Be32(preamble.data()+8);
    if(signature!=(stage==Stage::Vertex?0x102a1101u:0x102a1100u) ||
       !physical_bytes || virtual_bytes<36u ||
       virtual_bytes>(1u<<20) ||
       uint64_t(header_address)+virtual_bytes>
           uint64_t(UINT32_MAX)+1u)
      return std::nullopt;
    std::vector<uint8_t> header(virtual_bytes);
    if(!reader(original_guest_base,header_address,
               reinterpret_cast<std::byte*>(header.data()),header.size()))
      return std::nullopt;
    if(identities.FindOriginalSource(header,stage))
      return OriginalLiveXdkShaderHeaderWitness{
          object,header_address,true,std::move(header),
          exact_frame_sequence,exact_call_ordinal};
    // XDK variants can retain the executable program while rewriting the
    // filename in their debug record. Require the exact live metadata
    // outside that record AND the complete live physical byte digest.
    // This does not accept unknown, stripped, or edited program code.
    if(stage!=Stage::Pixel || physical_bytes>(1u<<20) ||
       !identities.HasPixelDebugCandidate(header))return {};
    std::array<uint8_t,4> pointer{};
    if(!reader(original_guest_base,object+24u,
               reinterpret_cast<std::byte*>(pointer.data()),pointer.size()))return {};
    const auto physical_at=Be32(pointer.data());
    if(!physical_at || uint64_t(physical_at)+physical_bytes>(uint64_t{1}<<32))return {};
    std::vector<uint8_t> physical(physical_bytes);
    if(!reader(original_guest_base,physical_at,reinterpret_cast<std::byte*>(physical.data()),physical.size()) ||
       !identities.FindPixelDebugAlias(header,physical))return {};
    OriginalLiveXdkShaderHeaderWitness result{
        object,header_address,true,std::move(header),
        exact_frame_sequence,exact_call_ordinal};
    result.alias_live_physical=std::move(physical);
    return result;
  } catch (...) {
    // A guest read, allocation or checksum error is never gameplay-fatal.
    return std::nullopt;
  }
}

std::optional<OriginalPixelSamplerControls> ReadOriginalPixelSamplerControls(
    std::span<const uint8_t> container,const Digest& expected_source) {
  if(container.size()<60 || container.size()>(2u<<20) ||
     Be32(container.data())!=0x102a1100u || SourceSha256(container)!=expected_source)
    return {};
  const auto virtual_size=Be32(container.data()+4),physical_size=Be32(container.data()+8);
  const auto desc=Be32(container.data()+24);
  if(uint64_t(virtual_size)+physical_size!=container.size() || desc<36 ||
     uint64_t(desc)+24>virtual_size)return {};
  const auto offset=Be32(container.data()+desc),bytes=Be32(container.data()+desc+4);
  if(!bytes || bytes%12 || offset%4 || uint64_t(offset)+bytes>physical_size)return {};
  const auto* code=container.data()+virtual_size+offset;
  const uint32_t count=bytes/12;
  uint32_t cf_end=count;
  OriginalPixelSamplerControls controls{expected_source};
  for(uint32_t i=0;i<cf_end;++i) {
    const auto a=Be32(code+i*12),b=Be32(code+i*12+4),c=Be32(code+i*12+8);
    const uint32_t words[]={a,(b>>16)|(c<<16)},ops[]={(b>>12)&15u,c>>28};
    for(unsigned half=0;half<2;++half) {
      const auto op=ops[half],cf=words[half];
      if(!((op>=1 && op<=6) || op==13 || op==14))continue;
      const auto addr=cf&4095u,length=(cf>>12)&7u;
      if(addr<=i || addr>count || length>6 || length>count-addr)return {};
      cf_end=std::min(cf_end,addr);
      for(uint32_t j=0;j<length;++j) {
        if(!(cf&(1u<<(16+2*j))))continue; // ALU instructions share this storage.
        const auto* inst=code+12*(addr+j);
        const auto f0=Be32(inst),f1=Be32(inst+4),f2=Be32(inst+8);
        const auto opcode=f0&31u;
        if(opcode==19 || opcode==25 || opcode==26)continue; // weights, set gradients
        if(opcode!=1 || (f0>>20&31u)>=16 || (f0&(1u<<25)) ||
           (f1>>18&7u)!=7 || (f1>>21&7u)!=0 || (f2>>2&127u)!=0)return {};
        const auto filters=(f1>>12)&63u;
        if(filters!=0u && filters!=63u)return {};
        const auto mask=static_cast<uint16_t>(1u<<(f0>>20&15u));
        const bool point=filters==0;
        if((controls.sampled_slots&mask) && bool(controls.point_slots&mask)!=point)
          return {}; // Same sampler object cannot implement two different policies.
        controls.sampled_slots|=mask;
        if(point)controls.point_slots|=mask;
      }
    }
  }
  if(cf_end==count)return {};
  return controls;
}

std::optional<OriginalPixelSamplerControls> CaptureOriginalPixelSamplerControls(
    const OriginalLiveXdkShaderHeaderWitness& witness,const Digest& source,
    ReadOriginalGuestShaderBytes read,void* state,OriginalPixelSamplerCache& cache,
    const OriginalVirtualShaderIdentity* identities) {
  const auto& header=witness.exact_original_guest_virtual_header;
  const uint32_t object=witness.original_xdk_shader_guest_object;
  if(!read || !witness.original_object_to_header_abi_verified || header.size()<36 ||
     header.size()>(1u<<20) || !object || UsaShaderHeaderAddress(object,Stage::Pixel)!=
        witness.original_virtual_header_guest_address)return {};
  if(!witness.alias_live_physical.empty()) {
    const auto* alias=identities ?
        identities->FindPixelDebugAlias(header,witness.alias_live_physical):nullptr;
    if(!alias || alias->full_source_sha!=source)return {};
    if(const auto known=cache.find(source);known!=cache.end()) {
      const auto& saved=known->second.container;
      if(saved.size()!=alias->original_header.size()+witness.alias_live_physical.size() ||
         !std::equal(alias->original_header.begin(),alias->original_header.end(),saved.begin()) ||
         !std::equal(witness.alias_live_physical.begin(),witness.alias_live_physical.end(),
                     saved.begin()+alias->original_header.size()))return {};
      return known->second.controls;
    }
    auto original=alias->original_header;
    original.insert(original.end(),witness.alias_live_physical.begin(),witness.alias_live_physical.end());
    const auto controls=ReadOriginalPixelSamplerControls(original,source);
    if(controls)cache.emplace(source,VerifiedPixelSamplerProgram{*controls,std::move(original)});
    return controls;
  }
  const auto size=Be32(header.data()+8);
  std::array<uint8_t,4> address{};
  // USA 82876EA0 copies the virtual part from object+40 and the physical
  // portion from the pointer at object+24 (82876EE8), with no address mask.
  if(!size || size>(1u<<20) || !read(state,object+24u,
        reinterpret_cast<std::byte*>(address.data()),address.size()))return {};
  const auto gpu_address=Be32(address.data());
  if(!gpu_address || uint64_t(gpu_address)+size>(uint64_t{1}<<32))return {};
  // The guest may recycle or rewrite either allocation between draws.
  // Reuse decoded controls only while both source segments remain identical.
  if(const auto known=cache.find(source);known!=cache.end()) {
    const auto& saved=known->second.container;
    if(saved.size()!=header.size()+size ||
       !std::equal(header.begin(),header.end(),saved.begin()))return {};
    std::array<uint8_t,1024> chunk;
    for(uint32_t offset=0;offset<size;) {
      const auto n=std::min<uint32_t>(size-offset,static_cast<uint32_t>(chunk.size()));
      if(!read(state,gpu_address+offset,reinterpret_cast<std::byte*>(chunk.data()),n) ||
         !std::equal(chunk.begin(),chunk.begin()+n,saved.begin()+header.size()+offset))return {};
      offset+=n;
    }
    return known->second.controls;
  }
  auto bytes=header;
  bytes.resize(header.size()+size);
  if(!read(state,gpu_address,reinterpret_cast<std::byte*>(bytes.data()+header.size()),size))
    return {};
  const auto controls=ReadOriginalPixelSamplerControls(bytes,source);
  if(controls)cache.emplace(source,VerifiedPixelSamplerProgram{*controls,std::move(bytes)});
  return controls;
}

std::optional<Digest> ResolveOriginalLiveXdkShaderSource(
    const OriginalVirtualShaderIdentity& identities,
    const OriginalLiveXdkShaderHeaderWitness& source,
    uint32_t currently_bound_xdk_shader_object,
    Stage expected_stage,
    uint64_t exact_frame_sequence,uint32_t exact_call_ordinal) noexcept {
  if(!exact_frame_sequence || exact_call_ordinal==UINT32_MAX ||
     source.original_frame_sequence!=exact_frame_sequence ||
     source.original_call_ordinal!=exact_call_ordinal ||
     !source.original_object_to_header_abi_verified ||
     !source.original_xdk_shader_guest_object ||
     source.original_xdk_shader_guest_object!=currently_bound_xdk_shader_object ||
     UsaShaderHeaderAddress(currently_bound_xdk_shader_object,expected_stage)!=
         source.original_virtual_header_guest_address ||
     uint64_t(source.original_virtual_header_guest_address)+
         source.exact_original_guest_virtual_header.size()>uint64_t(UINT32_MAX)+1u)
    return std::nullopt;
  if(const auto direct=identities.FindOriginalSource(
      source.exact_original_guest_virtual_header,expected_stage))return direct;
  if(expected_stage==Stage::Pixel && !source.alias_live_physical.empty())
    if(const auto* alias=identities.FindPixelDebugAlias(
         source.exact_original_guest_virtual_header,source.alias_live_physical))
      return alias->full_source_sha;
  return {};
}

std::optional<OriginalEffectPass> ResolveOriginalLiveEorEffectPass(
    const OriginalVirtualShaderIdentity& identities,
    const EffectCatalog& effects,
    const OriginalLiveXdkShaderHeaderWitness& vertex,
    const OriginalLiveXdkShaderHeaderWitness& pixel,
    uint32_t bound_vertex_xdk_object,uint32_t bound_pixel_xdk_object,
    uint64_t exact_frame_sequence,uint32_t exact_call_ordinal) noexcept {
  const auto vs=ResolveOriginalLiveXdkShaderSource(
      identities,vertex,bound_vertex_xdk_object,Stage::Vertex,
      exact_frame_sequence,exact_call_ordinal);
  const auto ps=ResolveOriginalLiveXdkShaderSource(
      identities,pixel,bound_pixel_xdk_object,Stage::Pixel,
      exact_frame_sequence,exact_call_ordinal);
  if(!vs || !ps)return std::nullopt;
  return effects.FindOriginalPass(*vs,*ps);
}

bool MatchesOriginalLiveXdkShaderSource(
    const OriginalVirtualShaderIdentity& identities,
    const OriginalLiveXdkShaderHeaderWitness& source,
    uint32_t currently_bound_xdk_shader_object,
    Stage expected_stage,
    const Digest& expected_original_full_shader_sha256,
    uint64_t exact_frame_sequence,uint32_t exact_call_ordinal) noexcept {
  if(expected_original_full_shader_sha256==Digest{})return false;
  const auto digest=ResolveOriginalLiveXdkShaderSource(
      identities,source,currently_bound_xdk_shader_object,
      expected_stage,exact_frame_sequence,exact_call_ordinal);
  return digest && *digest==expected_original_full_shader_sha256;
}
} // namespace hulk::eor::aot
