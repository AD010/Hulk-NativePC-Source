#include "eor_original_virtual_shader_identity.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <set>

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
  const auto virtual_sha=OriginalShaderVirtualSha256(exact_header);
  const auto it=original_.find(virtual_sha);
  if(it==original_.end() || it->second.stage!=expected_stage ||
     it->second.virtual_bytes!=exact_header.size())
    return std::nullopt;
  return it->second.full_source_sha;
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
    if(!identities.FindOriginalSource(header,stage))
      return std::nullopt;
    return OriginalLiveXdkShaderHeaderWitness{
        object,header_address,true,std::move(header),
        exact_frame_sequence,exact_call_ordinal};
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
    ReadOriginalGuestShaderBytes read,void* state,OriginalPixelSamplerCache& cache) {
  const auto& header=witness.exact_original_guest_virtual_header;
  const uint32_t object=witness.original_xdk_shader_guest_object;
  if(!read || !witness.original_object_to_header_abi_verified || header.size()<36 ||
     header.size()>(1u<<20) || !object || UsaShaderHeaderAddress(object,Stage::Pixel)!=
        witness.original_virtual_header_guest_address)return {};
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
      const auto n=std::min<uint32_t>(size-offset,chunk.size());
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
  return identities.FindOriginalSource(
      source.exact_original_guest_virtual_header,expected_stage);
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
