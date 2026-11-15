#include "../native_port/src/eor_effect_catalog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
using hulk::eor::aot::EffectCatalog;
static void Word(std::vector<uint8_t>& b,uint32_t v) {
  for(unsigned i=0;i<4;++i)b.push_back(uint8_t(v>>(i*8)));
}
static void Record(std::vector<uint8_t>& b,uint32_t effect,uint32_t stage,uint8_t id) {
  Word(b,effect);Word(b,stage);
  for(unsigned i=0;i<32;++i)b.push_back(id);
}
int main() {
  auto path=std::filesystem::temp_directory_path()/"hulk_effect_catalog_test.bin";
  std::vector<uint8_t> bytes{'H','E','O','R','M','A','T','1'};
  Word(bytes,4);Word(bytes,1);
  Record(bytes,0x111,1,0x11); Record(bytes,0x111,2,0x22);
  Record(bytes,0x222,1,0x33); Record(bytes,0x222,2,0x44);
  auto write=[&]{std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write((const char*)bytes.data(),bytes.size());};
  write();
  EffectCatalog c; c.Load(path);
  assert(c.EffectCount()==2 && c.ShaderCount()==4);
  hulk::eor::aot::Digest a{},b{},bad{}; a.fill(0x11);b.fill(0x22);bad.fill(0x44);
  assert(c.HasPair(0x111,a,b));
  assert(!c.HasPair(0x111,a,bad)); // genuine shader, WRONG original effect
  assert(!c.HasPair(0,a,b));
  bytes[16+40+4]=1;write(); // duplicate vertex stage, missing PS in first effect
  bool caught=false;try{c.Load(path);}catch(const std::invalid_argument&){caught=true;}
  assert(caught && c.HasPair(0x111,a,b)); // previously loaded state survives
  bytes.resize(bytes.size()-1);write();caught=false;
  try{c.Load(path);}catch(const std::invalid_argument&){caught=true;}
  assert(caught && c.HasPair(0x111,a,b));
  std::filesystem::remove(path);
}
