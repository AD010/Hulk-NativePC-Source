#include "../native_port/src/eor_original_texture_streams.h"
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
  const std::string name="building_brick01_ds";
  const auto th=ResourceHash(name), sh=ResourceHash(name+"_STREAM");
  auto index=Index::Decode(TestIndex(th,sh,true));
  for(size_t offset:{size_t(34),size_t(22)}){
    const auto record=Texture(name,sh,offset);
    auto link=textures::FindVerifiedOriginalTexStream(record,th,index);
    assert(link && link->original_stream_hash==sh && link->indexed_stream.size==32);
    assert(link->original_stream_hash_offset==4+name.size()+1+offset);
  }
  auto record=Texture(name,sh,34);
  Reject([&]{(void)textures::FindVerifiedOriginalTexStream(record,ResourceHash("wrong"),index);});
  auto broken=record;broken[4+name.size()+1+34]=std::byte(0);
  Reject([&]{(void)textures::FindVerifiedOriginalTexStream(broken,th,index);});
  broken=record;Replace32(broken,4+name.size()+1+22,sh);
  Reject([&]{(void)textures::FindVerifiedOriginalTexStream(broken,th,index);});
  broken=record;broken[0]=std::byte('B');
  Reject([&]{(void)textures::FindVerifiedOriginalTexStream(broken,th,index);});
  auto no_stream=Index::Decode(TestIndex(th,sh,false));
  assert(!textures::FindVerifiedOriginalTexStream(record,th,no_stream));
  const auto file=std::filesystem::temp_directory_path()/"hulk_original_texstream_test.arc";
  std::vector<std::byte> disk(36,std::byte(0x7e));
  Replace32(disk,0,32);
  Save(file,disk);
  auto link=textures::FindVerifiedOriginalTexStream(record,th,index);
  auto payload=textures::ReadOriginalTexStream(file,*link);
  assert(payload.size()==32 && payload.front()==std::byte(0));
  auto view=textures::ValidateOriginalTexStreamEnvelope(payload);
  assert(view.size()==28 && view.front()==std::byte(0x7e));
  auto malformed=payload;
  Replace32(malformed,0,28);
  Reject([&]{(void)textures::ValidateOriginalTexStreamEnvelope(malformed);});
  Reject([&]{(void)textures::ValidateOriginalTexStreamEnvelope(
      std::span<const std::byte>(payload).first(4));});
  malformed=payload;
  malformed[0]=std::byte(0x7e);
  Save(file,malformed);
  Reject([&]{(void)textures::ReadOriginalTexStream(file,*link);});
  std::filesystem::remove(file);
}
