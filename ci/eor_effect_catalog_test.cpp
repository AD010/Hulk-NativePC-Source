#include "../native_port/src/eor_effect_catalog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
using hulk::eor::aot::EffectCatalog;
static void Word(std::vector<uint8_t>& b,uint32_t v) {
  for(unsigned i=0;i<4;++i) b.push_back(uint8_t(v>>(8*i)));
}
static void Pair(std::vector<uint8_t>& b,uint32_t effect,uint32_t pass,
                 uint8_t vs,uint8_t ps) {
  Word(b,effect);Word(b,pass);
  for(unsigned i=0;i<32;++i)b.push_back(vs);
  for(unsigned i=0;i<32;++i)b.push_back(ps);
}
int main() {
  auto path=std::filesystem::temp_directory_path()/"hulk_eor_material_pairs_test.bin";
  std::vector<uint8_t> data{'H','E','O','R','M','A','T','2'};
  Word(data,3);Word(data,2);
  Pair(data,0x111,0,0x11,0x22);
  Pair(data,0x111,1,0x33,0x44);
  Pair(data,0x222,0,0x55,0x66);
  auto write=[&]{std::ofstream f(path,std::ios::binary|std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()),data.size());};
  write();
  EffectCatalog catalog;catalog.Load(path);
  assert(catalog.EffectCount()==2 && catalog.PairCount()==3);
  assert(catalog.ShaderCount()==6);
  hulk::eor::aot::Digest vs{},ps{};vs.fill(0x11);ps.fill(0x22);
  assert(catalog.HasPair(0x111,0,vs,ps));
  assert(!catalog.HasPair(0x111,1,vs,ps)); // wrong technique ordinal
  vs.fill(0x33);ps.fill(0x44);
  assert(catalog.HasPair(0x111,1,vs,ps));
  assert(!catalog.HasPair(0x111,0,vs,ps));
  assert(!catalog.HasPair(0x222,1,vs,ps));
  data[16+72+4]=2;write();
  bool failed=false;try {catalog.Load(path);}catch(const std::invalid_argument&){failed=true;}
  assert(failed && catalog.HasPair(0x111,1,vs,ps));
  data.resize(data.size()-1);write();failed=false;
  try {catalog.Load(path);}catch(const std::invalid_argument&){failed=true;}
  assert(failed && catalog.HasPair(0x111,1,vs,ps));
  data[7]='1';write();failed=false;
  try {catalog.Load(path);}catch(const std::invalid_argument&){failed=true;}
  assert(failed);
  std::filesystem::remove(path);
}
