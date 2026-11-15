#include "../native_port/src/eor_original_model_assets.h"
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
void U32(std::vector<std::byte>& v,uint32_t n,bool be) {
  for(int i=0;i<4;i++)v.push_back(std::byte(n>>(8*(be?3-i:i))));
}
void Replace32(std::vector<std::byte>& v,size_t at,uint32_t n,bool be) {
  for(int i=0;i<4;i++)v[at+i]=std::byte(n>>(8*(be?3-i:i)));
}
std::vector<std::byte> MakeModel(bool be) {
  std::vector<std::byte> d{std::byte(be?0:1),std::byte(be?1:0)};
  d.insert(d.end(),4,std::byte(0));
  for(char c:std::string("test_model"))d.push_back(std::byte(c));
  d.push_back(std::byte(0));d.push_back(std::byte(0));U32(d,1,be);
  for(char c:std::string("blinn1"))d.push_back(std::byte(c));
  d.push_back(std::byte(0));d.insert(d.end(),64,std::byte(0));return d;
}
std::vector<std::byte> MakeIndex(bool be,uint32_t key,uint32_t length) {
  std::vector<std::byte> index;U32(index,2,be);
  for(int i=0;i<3;i++)U32(index,0,be);
  const auto name_start=uint32_t(index.size());
  for(char c:std::string("Models"))index.push_back(std::byte(c));
  index.push_back(std::byte(0));
  const auto data_start=uint32_t(index.size());
  U32(index,1,be);U32(index,key,be);U32(index,4,be);U32(index,length,be);U32(index,0,be);
  const auto footer=uint32_t(index.size());U32(index,0,be);
  Replace32(index,4,name_start,be);Replace32(index,8,data_start,be);
  Replace32(index,12,footer,be);return index;
}
void Save(const std::filesystem::path& file,const std::vector<std::byte>& data) {
  std::ofstream out(file,std::ios::binary|std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}
}
int main() {
  const auto dir=std::filesystem::temp_directory_path();
  for(bool be:{false,true}) {
    const auto idx=dir/(be?"hulk_model_guest_test.ind":"hulk_model_host_test.ind");
    const auto arc=dir/(be?"hulk_model_guest_test.arc":"hulk_model_host_test.arc");
    const auto original=MakeModel(be);
    const auto key=ResourceHash("test_model");
    Save(idx,MakeIndex(be,key,uint32_t(original.size())));
    std::vector<std::byte> data(4,std::byte(0));
    data.insert(data.end(),original.begin(),original.end());Save(arc,data);
    auto models=model::OriginalModelAssets::Open(idx,arc);
    assert(models.IndexedModelCount()==1);
    auto loaded=models.LoadName("test_model");
    assert(loaded && loaded->source.model_name=="test_model" &&
           loaded->source.first_surface_name=="blinn1" &&
           loaded->source.surface_records==1 && loaded->name_hash_matches);
    assert(loaded->bytes==original);
    assert(!models.LoadName("model_missing"));
    data[4]=std::byte(0xfe);Save(arc,data);
    bool rejected=false;
    try{(void)models.LoadHash(key);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    std::filesystem::remove(idx);std::filesystem::remove(arc);
  }
}
