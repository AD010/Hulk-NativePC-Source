// Private corpus regression: pass HEORVRT1.bin HEORMAT2.bin and the
// directory containing the owner's original USA SHADEREF.ARC.
// No original game bytes or private archive contents are embedded.
#include "../native_port/src/eor_original_virtual_shader_identity.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
using namespace hulk::eor::aot;
uint32_t Be(const uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
void Put(std::vector<uint8_t>& b,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(v>>(24-8*i));}
bool Read(void* s,uint32_t at,std::byte* out,size_t n){auto& b=*static_cast<std::vector<uint8_t>*>(s);if(uint64_t(at)+n>b.size())return false;std::memcpy(out,b.data()+at,n);return true;}
int main(int argc,char** argv){
 assert(argc==4);OriginalVirtualShaderIdentity ids;ids.Load(argv[1]);EffectCatalog effects;effects.Load(argv[2]);
 const std::filesystem::path root=argv[3];assert(ids.LoadOriginalPixelDebugAliases(root)==2595 && ids.PixelMetadataAliasCount()==1601);
 std::ifstream f(root/"SHADEREF.ARC",std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
 std::map<Digest,std::span<const uint8_t>> vs_headers,ps_containers;std::map<Digest,Digest> pair;
 effects.ForEachPair([&](uint32_t,uint32_t,const Digest& v,const Digest& p){assert(pair.emplace(p,v).second);assert(effects.UniquePixelForVertex(v)==p);});
 for(size_t i=0;i+36<=data.size();++i){
  auto flag=Be(data.data()+i);if(flag!=0x102a1100u && flag!=0x102a1101u)continue;
  auto v=Be(data.data()+i+4),p=Be(data.data()+i+8);if(v<36 || !p || uint64_t(i)+v+p>data.size())continue;
  auto header=std::span<const uint8_t>(data).subspan(i,v),full=std::span<const uint8_t>(data).subspan(i,v+p);
  auto stage=(flag&1)?Stage::Vertex:Stage::Pixel;auto sha=ids.FindOriginalSource(header,stage);
  if(!sha || SourceSha256(full)!=*sha)continue;
  assert((stage==Stage::Vertex?vs_headers.emplace(*sha,header):ps_containers.emplace(*sha,full)).second);i+=v+p-1;
 }
 assert(vs_headers.size()==2597 && ps_containers.size()==2597 && pair.size()==2597);
 std::vector<uint8_t> guest(3u<<20);constexpr uint32_t vo=0x1000,po=0x110000,code=0x220000;
 size_t paired=0,unpaired=0,sampling=0,sampling_unsupported=0;OriginalPixelSamplerCache cache;
 for(const auto& [ps,full]:ps_containers){
  const auto vs=pair.at(ps);const auto vh=vs_headers.at(vs);
  const auto v=Be(full.data()+4),ctab=Be(full.data()+16),defs=Be(full.data()+20),desc=Be(full.data()+24),removed=ctab-36u;
  auto physical=full.subspan(v);std::vector<uint8_t> stripped(full.begin(),full.begin()+36);stripped.insert(stripped.end(),full.begin()+ctab,full.begin()+v);
  Put(stripped,4,uint32_t(stripped.size()));Put(stripped,12,0);Put(stripped,16,36);Put(stripped,20,defs?defs-removed:0);Put(stripped,24,desc-removed);
  auto original_key=OriginalPixelMetadataKey(full.first(v));assert(original_key && OriginalPixelMetadataKey(stripped)==original_key);
  assert(!ids.FindOriginalSource(stripped,Stage::Pixel));
  const auto* direct=ids.FindPixelDebugAlias(stripped,physical);if(direct){assert(direct->full_source_sha==ps);++unpaired;}else ++paired;
  const auto* selected=ids.FindPixelDebugAlias(stripped,physical,&ps);assert(selected && selected->full_source_sha==ps);
  std::copy(vh.begin(),vh.end(),guest.begin()+vo+872);std::copy(stripped.begin(),stripped.end(),guest.begin()+po+40);Put(guest,po+24,code);std::copy(physical.begin(),physical.end(),guest.begin()+code);
  auto observed_vs=ObserveOriginalBoundXdkShader(ids,vo,Stage::Vertex,Read,&guest,7,0);assert(observed_vs);
  auto resolved_vs=ResolveOriginalLiveXdkShaderSource(ids,*observed_vs,vo,Stage::Vertex,7,0);assert(resolved_vs==vs);
  auto expected=effects.UniquePixelForVertex(*resolved_vs);assert(expected==ps);
  auto observed_ps=ObserveOriginalBoundXdkShader(ids,po,Stage::Pixel,Read,&guest,7,0,&*expected);assert(observed_ps);
  assert(bool(observed_ps->alias_paired_pixel_source)==!direct);
  assert(ResolveOriginalLiveXdkShaderSource(ids,*observed_ps,po,Stage::Pixel,7,0)==ps);
  auto pass=ResolveOriginalLiveEorEffectPass(ids,effects,*observed_vs,*observed_ps,vo,po,7,0);assert(pass && effects.HasPair(pass->effect_hash,pass->pass_index,vs,ps));
  assert(!ResolveOriginalLiveEorEffectPass(ids,effects,*observed_vs,*observed_ps,vo,po,7,1));
  if(!direct){
   assert(!ObserveOriginalBoundXdkShader(ids,po,Stage::Pixel,Read,&guest,7,0));
   auto wrong=vs_headers.begin();if(wrong->first==vs)++wrong;
   auto other=*observed_vs;other.exact_original_guest_virtual_header.assign(wrong->second.begin(),wrong->second.end());
   assert(!ResolveOriginalLiveEorEffectPass(ids,effects,other,*observed_ps,vo,po,7,0));
  }
  auto native=ReadOriginalPixelSamplerControls(full,ps);
  auto recovered=CaptureOriginalPixelSamplerControls(*observed_ps,ps,Read,&guest,cache,&ids);
  assert(bool(native)==bool(recovered));if(native){assert(native->sampled_slots==recovered->sampled_slots&&native->point_slots==recovered->point_slots);++sampling;}else ++sampling_unsupported;
  auto corrupt=*observed_ps;corrupt.alias_live_physical[0]^=1;assert(!ResolveOriginalLiveEorEffectPass(ids,effects,*observed_vs,corrupt,vo,po,7,0));
  corrupt=*observed_ps;corrupt.exact_original_guest_virtual_header.back()^=1;assert(!ResolveOriginalLiveEorEffectPass(ids,effects,*observed_vs,corrupt,vo,po,7,0));
 }
 assert(unpaired==1601 && paired==996 && sampling==2597 && sampling_unsupported==0);
 std::cout<<"PASS: 2597 original stripped PS, "<<paired<<" newly disambiguated, "<<unpaired<<" unambiguous, sampler decoder supported="<<sampling<<" unsupported="<<sampling_unsupported<<"\n";
}
