#include "../native_port/src/eor_original_shader_samplers.h"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace hulk::eor::aot;
namespace {
void Word(std::vector<uint8_t>& out,uint32_t n) {
  for(unsigned i=0;i<4;++i)out.push_back(uint8_t(n>>(8*i)));
}
void Add(std::vector<uint8_t>& out,const Digest& sha,uint32_t stage,
         uint32_t reg,const std::string& name) {
  for(uint8_t b:sha)out.push_back(b);
  Word(out,stage);
  Word(out,reg);
  assert(name.size()<32);
  for(char c:name)out.push_back(static_cast<uint8_t>(c));
  out.insert(out.end(),32-name.size(),0u);
}
void Write(const std::filesystem::path& path,
           const std::vector<uint8_t>& source) {
  std::ofstream f(path,std::ios::binary|std::ios::trunc);
  f.write(reinterpret_cast<const char*>(source.data()),
          static_cast<std::streamsize>(source.size()));
  if(!f)throw std::runtime_error("Synthetic XDK sampler fixture write failed");
}
template<typename F> void Reject(F action) {
  bool failed=false;
  try{action();}catch(const std::invalid_argument&){failed=true;}
  assert(failed);
}
}
int main() {
  const auto path=std::filesystem::temp_directory_path()/
       "hulk_xdk_source_samplers_synthetic.bin";
  Digest zero{},vs{},ps{};
  vs.fill(0x11);
  ps.fill(0x22);
  std::vector<uint8_t> source{'H','E','O','R','S','M','P','1'};
  Word(source,3);
  Word(source,1);
  Add(source,vs,1,UINT32_MAX,"");
  Add(source,ps,2,0,"DiffuseMap");
  Add(source,ps,2,1,"NormalMap");
  Write(path,source);
  OriginalXdkShaderSamplers catalog;
  catalog.Load(path);
  assert(catalog.ShaderCount()==2);
  const auto* vertex=catalog.Find(vs,Stage::Vertex);
  const auto* pixel=catalog.Find(ps,Stage::Pixel);
  assert(vertex && vertex->empty() && pixel && pixel->size()==2);
  assert((*pixel)[0].original_sampler_register==0);
  assert((*pixel)[0].original_sampler_name=="DiffuseMap");
  assert((*pixel)[1].original_sampler_register==1);
  assert((*pixel)[1].original_sampler_name=="NormalMap");
  assert(!catalog.Find(vs,Stage::Pixel));
  assert(!catalog.Find(zero,Stage::Vertex));
  auto bad=source;
  bad[8]=4; // count=4, inconsistent with the exact file length
  Write(path,bad);
  Reject([&]{catalog.Load(path);});
  assert(catalog.ShaderCount()==2);
  bad=source;
  // Second PS sampler uses the same original XDK sampler register.
  const size_t third=16+72*2;
  bad[third+36]=0;
  Write(path,bad);
  Reject([&]{catalog.Load(path);});
  bad=source;
  // Reusing source descriptor names is ambiguous even for distinct registers.
  const auto second=16+72;
  for(size_t i=0;i<32;++i)bad[third+40+i]=bad[second+40+i];
  Write(path,bad);
  Reject([&]{catalog.Load(path);});
  bad=source;
  bad[third+32]=1; // PS digest changes stage between its two slots
  Write(path,bad);
  Reject([&]{catalog.Load(path);});
  bad=source;
  bad[third+40]='/'; // unsafe shader variable
  Write(path,bad);
  Reject([&]{catalog.Load(path);});
  std::error_code ec;
  std::filesystem::remove(path,ec);
}
