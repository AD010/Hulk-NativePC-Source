#include "../native_port/src/eor_original_texture_streams.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor;
namespace {
void U32(std::vector<std::byte>& v,uint32_t n){for(int i=3;i>=0;--i)v.push_back(std::byte(n>>(8*i)));}
void Replace32(std::vector<std::byte>& v,size_t at,uint32_t n){
  for(int i=3;i>=0;--i)v[at+(3-i)]=std::byte(n>>(8*i));
}
std::vector<std::byte> TestIndex(uint32_t tex,uint32_t stream,bool include_stream){
  std::vector<std::byte> data;
  U32(data,4);
  for(int i=0;i<5;++i)U32(data,0);
  const uint32_t textures=uint32_t(data.size());
  for(char c:std::string("Textures"))data.push_back(std::byte(c));
  data.push_back(std::byte(0));
  const uint32_t textures_table=uint32_t(data.size());
  U32(data,1);U32(data,tex);U32(data,4);U32(data,128);U32(data,0);
  const uint32_t streams_name=uint32_t(data.size());
  for(char c:std::string("TexStreams"))data.push_back(std::byte(c));
  data.push_back(std::byte(0));
  const uint32_t streams_table=uint32_t(data.size());
  U32(data,include_stream?1:0);
  if(include_stream){U32(data,stream);U32(data,4);U32(data,32);U32(data,0);}
  const uint32_t footer=uint32_t(data.size());U32(data,0);
  Replace32(data,4,textures);Replace32(data,8,textures_table);
  Replace32(data,12,streams_name);Replace32(data,16,streams_table);
  Replace32(data,20,footer);
  return data;
}
void Save(const std::filesystem::path& p,const std::vector<std::byte>& b){
  std::ofstream f(p,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
}
std::vector<std::byte> Texture(std::string name,uint32_t ref,size_t rel){
  std::vector<std::byte> v;
  for(char c:std::string("LFXT")+name)v.push_back(std::byte(c));
  v.push_back(std::byte(0));v.resize(v.size()+128,std::byte(0));
  const size_t pos=4+name.size()+1+rel;
  Replace32(v,pos,ref);
  return v;
}
template<class F> void Reject(F f){
  bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}assert(caught);
}
}
int main(){
  const std::string fixture_texture_name="building_brick01_ds";
  const auto th=ResourceHash(fixture_texture_name), sh=ResourceHash(fixture_texture_name+"_STREAM");
  auto index=Index::Decode(TestIndex(th,sh,true));
  for(size_t offset:{size_t(34),size_t(22)}){
    const auto record=Texture(fixture_texture_name,sh,offset);
    auto link=textures::FindVerifiedOriginalTexStream(record,th,index);
    assert(link && link->original_stream_hash==sh && link->indexed_stream.size==32);
    assert(link->original_stream_hash_offset==4+fixture_texture_name.size()+1+offset);
  }
  auto record=Texture(fixture_texture_name,sh,34);
  Reject([&]{(void)textures::FindVerifiedOriginalTexStream(record,ResourceHash("wrong"),index);});
  auto broken=record;broken[4+fixture_texture_name.size()+1+34]=std::byte(0);
  Reject([&]{(void)textures::FindVerifiedOriginalTexStream(broken,th,index);});
  broken=record;Replace32(broken,4+fixture_texture_name.size()+1+22,sh);
  Reject([&]{(void)textures::FindVerifiedOriginalTexStream(broken,th,index);});
  broken=record;broken[0]=std::byte('B');
  Reject([&]{(void)textures::FindVerifiedOriginalTexStream(broken,th,index);});
  auto no_stream=Index::Decode(TestIndex(th,sh,false));
  assert(!textures::FindVerifiedOriginalTexStream(record,th,no_stream));
  const auto file=std::filesystem::temp_directory_path()/"hulk_original_texstream_test.arc";
  std::vector<std::byte> disk(36,std::byte(0x7e));
  Replace32(disk,4,28);
  Save(file,disk);
  auto link=textures::FindVerifiedOriginalTexStream(record,th,index);
  auto payload=textures::ReadOriginalTexStream(file,*link);
  assert(payload.size()==32 && payload.front()==std::byte(0));
  auto view=textures::ValidateOriginalTexStreamEnvelope(payload);
  assert(view.size()==28 && view.front()==std::byte(0x7e));
  auto malformed=payload;
  // The valid 32-byte source record already declares a 28-byte body.
  // Changing 28 to 28 was a no-op and could never be rejected.
  Replace32(malformed,0,27);
  Reject([&]{(void)textures::ValidateOriginalTexStreamEnvelope(malformed);});
  Reject([&]{(void)textures::ValidateOriginalTexStreamEnvelope(
      std::span<const std::byte>(payload).first(4));});
  malformed=payload;
  malformed[0]=std::byte(0x7e);
  std::copy(malformed.begin(),malformed.end(),disk.begin()+4);
  Save(file,disk);
  Reject([&]{(void)textures::ReadOriginalTexStream(file,*link);});
  std::filesystem::remove(file);
  // Ten original USA Xbox LFXT header sizes and nested remaining-length
  // positions measured in the user's actual (SHA-pinned) INDEX.IND report.
  // No copyrighted texture blocks are distributed as test fixtures.
  struct OriginalSize {const char* name;size_t length;size_t first;};
  constexpr OriginalSize originals[]{
    {"building_brick41c_grunge_ds",32927u,99u},
    {"signswap_64x32_eats10_d",16523u,79u},
    {"building_empirel_n",32918u,90u},
    {"frontend_lightmap_dir",524421u,73u},
    {"bldg_city_mt0098_egc_marble02b_ds",32933u,105u},
    {"perimeter_sidewalk01_curb_n",49295u,83u},
    {"storefront_mid_016d_n",32929u,101u},
    {"building_windows07a_n",32921u,93u},
    {"bf_area1_lightmap_dir",262277u,73u},
    {"signswap_32x32_misc01_d",16523u,79u}
  };
  for(const auto& example:originals) {
    std::vector<std::byte> lfxt(example.length,std::byte{0});
    for(size_t i=0;i<4;++i)
      lfxt[i]=std::byte(static_cast<unsigned char>("LFXT"[i]));
    const std::string name{example.name};
    for(size_t i=0;i<name.size();++i)
      lfxt[4+i]=std::byte(static_cast<unsigned char>(name[i]));
    // name terminator already zero from initialization.
    Replace32(lfxt,example.first,uint32_t(example.length-example.first-4));
    Replace32(lfxt,example.first+4,uint32_t(example.length-example.first-8));
    const auto layout=textures::FindOriginalLfxtInlineSection(
       lfxt,ResourceHash(name));
    assert(layout &&
           layout->outer_length_field_offset==example.first &&
           layout->inner_length_field_offset==example.first+4 &&
           layout->inline_data_offset==example.first+8 &&
           layout->inline_data_bytes==example.length-example.first-8);
    assert(layout->inline_data_offset+layout->inline_data_bytes==lfxt.size());
    assert((layout->inline_data_bytes-52u)%4096u==0u);
    Reject([&]{(void)textures::FindOriginalLfxtInlineSection(
      lfxt,ResourceHash("some_other_original_texture"));});
    lfxt[example.first]=std::byte{0x7f};
    assert(!textures::FindOriginalLfxtInlineSection(
        lfxt,ResourceHash(name)));
  }
  // An actual original USA descriptor table has variable length but its
  // count and aligned stream spans MUST agree with the original indexed
  // TexStreams resource. These are the ten verified metadata-only examples.
  struct OriginalStreamMetadata {
    const char* name;
    size_t texture_bytes, outer_at, relative_stream_reference;
    uint32_t stream_bytes;
    std::vector<uint32_t> original_aligned_levels;
    std::vector<float> metrics;
  };
  const OriginalStreamMetadata original_tables[]{
    {"building_brick41c_grunge_ds",32927u,99u,34u,98360u,{65536u,16384u,16384u},{256.000000f,128.000000f,64.000000f}},
    {"signswap_64x32_eats10_d",16523u,79u,34u,8248u,{8192u},{90.509670f}},
    {"building_empirel_n",32918u,90u,34u,360504u,{262144u,65536u,32768u},{512.000000f,256.000000f,128.000000f}},
    {"frontend_lightmap_dir",524421u,73u,22u,10485816u,{8388608u,2097152u},{1024.000000f,512.000000f}},
    {"bldg_city_mt0098_egc_marble02b_ds",32933u,105u,34u,98360u,{65536u,16384u,16384u},{256.000000f,128.000000f,64.000000f}},
    {"perimeter_sidewalk01_curb_n",49295u,83u,34u,65592u,{65536u},{181.019300f}},
    {"storefront_mid_016d_n",32929u,101u,34u,360504u,{262144u,65536u,16384u,16384u},{512.000000f,256.000000f,128.000000f,64.000000f}},
    {"building_windows07a_n",32921u,93u,34u,180280u,{131072u,32768u,16384u},{362.038670f,181.019330f,90.509667f}},
    {"bf_area1_lightmap_dir",262277u,73u,22u,5242936u,{4194304u,1048576u},{724.077330f,362.038670f}},
    {"signswap_32x32_misc01_d",16523u,79u,34u,8248u,{8192u},{64.000000f}}
  };
  for(const auto& example:original_tables) {
    std::vector<std::byte> original_lfxt(example.texture_bytes,std::byte{0});
    for(size_t i=0;i<4;++i)
      original_lfxt[i]=std::byte(static_cast<unsigned char>("LFXT"[i]));
    const std::string name{example.name};
    for(size_t i=0;i<name.size();++i)
      original_lfxt[i+4]=std::byte(static_cast<unsigned char>(name[i]));
    uint32_t resident_width=32,resident_height=32;
    if(name=="signswap_64x32_eats10_d" ||
       name=="building_windows07a_n") {
      resident_width=64;resident_height=32;
    } else if(name=="building_empirel_n") {
      resident_width=128;resident_height=32;
    } else if(name=="frontend_lightmap_dir") {
      resident_width=256;resident_height=256;
    } else if(name=="perimeter_sidewalk01_curb_n") {
      resident_width=256;resident_height=32;
    } else if(name=="bf_area1_lightmap_dir") {
      resident_width=256;resident_height=128;
    }
    const size_t name_base=4+name.size()+1;
    original_lfxt[name_base+3]=std::byte(resident_width>>8);
    original_lfxt[name_base+4]=std::byte(resident_width&255u);
    original_lfxt[name_base+7]=std::byte(resident_height>>8);
    original_lfxt[name_base+8]=std::byte(resident_height&255u);
    Replace32(original_lfxt,name_base+9,
      std::bit_cast<uint32_t>(static_cast<float>(
          std::sqrt(static_cast<double>(resident_width)*resident_height))));
    const size_t reference=4+name.size()+1+example.relative_stream_reference;
    textures::OriginalStreamLink identified{};
    identified.original_texture_hash=ResourceHash(name);
    identified.original_stream_hash=ResourceHash(name+"_STREAM");
    identified.original_stream_hash_offset=reference;
    identified.indexed_stream={identified.original_stream_hash,4,
                               example.stream_bytes,0};
    Replace32(original_lfxt,reference,identified.original_stream_hash);
    const size_t count_at=reference+8;
    assert(example.original_aligned_levels.size()==example.metrics.size());
    assert(example.original_aligned_levels.size()<=14);
    original_lfxt[count_at]=std::byte(example.original_aligned_levels.size());
    for(size_t i=0;i<example.original_aligned_levels.size();++i) {
      const size_t at=count_at+1+8*i;
      Replace32(original_lfxt,at,example.original_aligned_levels[i]);
      Replace32(original_lfxt,at+4,
                std::bit_cast<uint32_t>(example.metrics[i]));
    }
    Replace32(original_lfxt,example.outer_at,
              uint32_t(example.texture_bytes-example.outer_at-4));
    Replace32(original_lfxt,example.outer_at+4,
              uint32_t(example.texture_bytes-example.outer_at-8));
    auto parsed=textures::ParseOriginalLfxtStreamTable(original_lfxt,identified);
    assert(parsed && parsed->levels.size()==example.metrics.size() &&
           parsed->original_count_offset==count_at &&
           parsed->original_entries_offset==count_at+1);
    uint64_t sum=0;
    for(size_t i=0;i<parsed->levels.size();++i) {
      assert(parsed->levels[i].source_aligned_bytes==
             example.original_aligned_levels[i]);
      sum+=parsed->levels[i].source_aligned_bytes;
    }
    assert(sum==parsed->summed_source_bytes &&
           sum+56==example.stream_bytes);
    const auto inline_section=textures::FindOriginalLfxtInlineSection(
        original_lfxt,ResourceHash(name));
    assert(inline_section);
    const auto mip_plan=textures::ReconstructOriginalLfxtMipPlan(
        original_lfxt,ResourceHash(name),*inline_section,*parsed);
    assert(mip_plan && mip_plan->levels.size()>=parsed->levels.size() &&
           !mip_plan->absolute_source_offsets_proven);
    auto wrong_metric=original_lfxt;
    Replace32(wrong_metric,4+name.size()+1+9,0);
    assert(!textures::ReconstructOriginalLfxtMipPlan(
        wrong_metric,ResourceHash(name),*inline_section,*parsed));
    for(size_t level=0;level<parsed->levels.size();++level) {
      assert(mip_plan->levels[level].from_stream_archive);
      assert(mip_plan->levels[level].aligned_storage_bytes==
             example.original_aligned_levels[level]);
    }
    auto corrupt_inline=*inline_section;
    corrupt_inline.inline_data_bytes-=4;
    assert(!textures::ReconstructOriginalLfxtMipPlan(
        original_lfxt,ResourceHash(name),corrupt_inline,*parsed));
    original_lfxt[count_at]=std::byte(0);
    assert(!textures::ParseOriginalLfxtStreamTable(original_lfxt,identified));
    original_lfxt[count_at]=std::byte(example.original_aligned_levels.size());
    Replace32(original_lfxt,count_at+1,1);
    assert(!textures::ParseOriginalLfxtStreamTable(original_lfxt,identified));
    Replace32(original_lfxt,count_at+1,example.original_aligned_levels[0]);
    Replace32(original_lfxt,count_at+5,0x7f800000u);
    assert(!textures::ParseOriginalLfxtStreamTable(original_lfxt,identified));
  }
  // Ambiguous double candidates cannot select an arbitrary wrong GPU region.
  std::vector<std::byte> dup(2048,std::byte{0});
  for(size_t i=0;i<4;++i)dup[i]=std::byte(static_cast<unsigned char>("LFXT"[i]));
  const std::string dupname="sample";
  for(size_t i=0;i<dupname.size();++i)
    dup[4+i]=std::byte(static_cast<unsigned char>(dupname[i]));
  for(size_t pos:{size_t(72),size_t(88)}) {
    Replace32(dup,pos,uint32_t(dup.size()-pos-4));
    Replace32(dup,pos+4,uint32_t(dup.size()-pos-8));
  }
  Reject([&]{(void)textures::FindOriginalLfxtInlineSection(
       dup,ResourceHash(dupname));});
  // Genuine source record lengths from ten uploaded TexStreams samples.
  // Synthetic contents test only the BE framing, not a guessed GPU format.
  for (const size_t len : {size_t(98360), size_t(8248),
       size_t(360504), size_t(10485816), size_t(65592),
       size_t(180280), size_t(5242936)}) {
    std::vector<std::byte> example(len,std::byte{0});
    Replace32(example,0,static_cast<uint32_t>(len-4));
    const auto body=textures::ValidateOriginalTexStreamEnvelope(example);
    assert(body.size()==len-4 && body.data()==example.data()+4);
    const auto first=textures::VerifiedOriginalStreamByteRange(example,4,1);
    assert(first.data()==example.data()+4 && first.size()==1);
    const auto last=textures::VerifiedOriginalStreamByteRange(example,len-1,1);
    assert(last.data()==example.data()+len-1);
    Reject([&]{(void)textures::VerifiedOriginalStreamByteRange(example,0,1);});
    Reject([&]{(void)textures::VerifiedOriginalStreamByteRange(example,3,1);});
    Reject([&]{(void)textures::VerifiedOriginalStreamByteRange(example,len,1);});
    Reject([&]{(void)textures::VerifiedOriginalStreamByteRange(example,4,0);});
    Reject([&]{(void)textures::VerifiedOriginalStreamByteRange(example,len-1,2);});
  }
}
