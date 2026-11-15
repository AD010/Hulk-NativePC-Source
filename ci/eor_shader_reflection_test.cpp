#include "../native_port/src/eor_shader_reflection.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::aot;
static void W(std::vector<uint8_t>& b,uint32_t x){
  for(int k=0;k<4;++k)b.push_back(uint8_t(x>>(8*k)));
}
static void Entry(std::vector<uint8_t>& b,uint8_t key,int stage,bool projection){
  for(int k=0;k<32;++k)b.push_back(key);
  W(b,uint32_t(stage));
  W(b,projection?11:UINT32_MAX);W(b,projection?4:0);
  W(b,projection?7:UINT32_MAX);W(b,projection?3:0);
  for(int i=0;i<2;++i){W(b,UINT32_MAX);W(b,0);}
}
int main(){
  auto path=std::filesystem::temp_directory_path()/"hulk_reflection_synthetic.bin";
  std::vector<uint8_t> b{'H','E','O','R','R','E','F','1'};
  W(b,2);W(b,1);Entry(b,0x11,1,true);Entry(b,0x22,2,false);
  auto write=[&]{std::ofstream f(path,std::ios::binary|std::ios::trunc);
    f.write((char*)b.data(),b.size());};write();
  ReflectionBank bank;bank.Load(path);assert(bank.size()==2);
  Digest vs{},ps{};vs.fill(0x11);ps.fill(0x22);
  auto v=bank.Find(vs,Stage::Vertex);
  assert(v && v->view_projection.Present() && v->view_projection.index==11);
  assert(v->world.Present() && v->world.index==7 &&
         v->world.count==3);
  assert(!v->previous_world.Present());
  assert(!bank.Find(vs,Stage::Pixel));
  assert(bank.Find(ps,Stage::Pixel) && !bank.Find(ps,Stage::Pixel)->view_projection.Present());
  b[16+36+4]=0xff;write();
  bool failed=false;try{bank.Load(path);}catch(const std::invalid_argument&){failed=true;}
  assert(failed && bank.Find(vs,Stage::Vertex)->view_projection.index==11);
  b.resize(b.size()-1);write();failed=false;
  try{bank.Load(path);}catch(const std::invalid_argument&){failed=true;}
  assert(failed && bank.size()==2);
  std::filesystem::remove(path);
}
