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
  EffectCatalog copied;copied.Load(path);
  assert(&copied!=&catalog && catalog.SameOriginalSources(copied));
  auto alternate=data;
  alternate[16+8]^=1; // different VS digest, same effect and stage count
  {
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    f.write(reinterpret_cast<const char*>(alternate.data()),alternate.size());
  }
  EffectCatalog different;different.Load(path);
  assert(different.EffectCount()==catalog.EffectCount() &&
         different.ShaderCount()==catalog.ShaderCount() &&
         !catalog.SameOriginalSources(different));
  write();
  assert(catalog.ShaderCount()==6);
  hulk::eor::aot::Digest vs{},ps{};vs.fill(0x11);ps.fill(0x22);
  assert(catalog.HasPair(0x111,0,vs,ps));
  assert(catalog.UniquePixelForVertex(vs)==ps);
  assert(catalog.UniqueVertexForPixel(ps)==vs);
  const auto first_original_pass=catalog.FindOriginalPass(vs,ps);
  assert(first_original_pass && first_original_pass->effect_hash==0x111 &&
         first_original_pass->pass_index==0);
  assert(!catalog.HasPair(0x111,1,vs,ps)); // wrong technique ordinal
  vs.fill(0x33);ps.fill(0x44);
  assert(catalog.HasPair(0x111,1,vs,ps));
  assert(!catalog.HasPair(0x111,0,vs,ps));
  assert(!catalog.HasPair(0x222,1,vs,ps));
  // A single native compiled shader can be shared by many effect passes.
  data[8]=4; // four material/pass entries but just six distinct shader digests
  Pair(data,0x222,1,0x11,0x66);
  write();catalog.Load(path);
  assert(catalog.PairCount()==4 && catalog.EffectCount()==2 &&
         catalog.ShaderCount()==6);
  hulk::eor::aot::Digest reused_vs{},reused_ps{};
  reused_vs.fill(0x11);reused_ps.fill(0x66);
  assert(catalog.HasPair(0x222,1,reused_vs,reused_ps));
  assert(!catalog.UniquePixelForVertex(reused_vs)); // reused VS must not choose an alias
  const auto compile_hint=catalog.CompilationPassForVertex(reused_vs);
  assert(compile_hint&&compile_hint->effect_hash==0x111&&compile_hint->pass_index==0);
  assert(!catalog.CompilationPassForVertex(hulk::eor::aot::Digest{}));
  assert(!catalog.CompilationPassForVertex(reused_ps)); // pixel digest is not a VS
  assert(!catalog.UniqueVertexForPixel(reused_ps)); // shared PS also stays ambiguous
  assert(catalog.UniqueVertexForPixel(ps)==vs);
  assert(catalog.UniquePixelForVertex(vs)==ps);
  const auto shared_original_pass=catalog.FindOriginalPass(reused_vs,reused_ps);
  assert(shared_original_pass && shared_original_pass->effect_hash==0x222 &&
         shared_original_pass->pass_index==1);
  hulk::eor::aot::Digest absent{};absent.fill(0x88);
  const std::array vertices{reused_vs,absent};
  const std::array pixels{reused_ps,absent};
  assert(catalog.UniqueLivePair(vertices,pixels)==
         (std::pair{reused_vs,reused_ps}));
  // Add a second actual matching candidate for the same VS.
  hulk::eor::aot::Digest other_pixel{};other_pixel.fill(0x22);
  const std::array two_matches{reused_ps,other_pixel};
  assert(!catalog.UniqueLivePair(vertices,two_matches));
  assert(!catalog.UniqueLivePair(std::span<const hulk::eor::aot::Digest>{},pixels));
  assert(!catalog.CompilationPassForVertex(absent));
  const auto verified_shared=data;
  // Duplicate exact stage pairs are ambiguous even when both effects
  // legitimately reuse one stage of a different original shader pair.
  for(unsigned k=0;k<32;++k) {
    data[16+3*72+8+k]=0x11;
    data[16+3*72+40+k]=0x22;
  }
  write();
  bool duplicate_failed=false;
  try {catalog.Load(path);}catch(const std::invalid_argument&){
    duplicate_failed=true;
  }
  const auto surviving=catalog.FindOriginalPass(reused_vs,reused_ps);
  assert(duplicate_failed && surviving &&
         surviving->effect_hash==0x222 && surviving->pass_index==1);
  data=verified_shared;
  for(unsigned k=0;k<32;++k) data[16+3*72+8+k]=0x22;
  // The ENTIRE VS digest now equals the PS digest of another material.
  write();bool stage_failed=false;
  try {catalog.Load(path);}catch(const std::invalid_argument&){stage_failed=true;}
  assert(stage_failed && catalog.PairCount()==4 &&
         catalog.HasPair(0x222,1,reused_vs,reused_ps));
  assert(!catalog.UniqueVertexForPixel(reused_ps));
  assert(catalog.UniqueVertexForPixel(ps)==vs);
  // A valid replacement ledger must clear the old ambiguity index.
  data=verified_shared;data.resize(16+3*72);data[8]=3;write();catalog.Load(path);
  hulk::eor::aot::Digest original_vs{};original_vs.fill(0x55);
  assert(catalog.UniqueVertexForPixel(reused_ps)==original_vs);
  data=verified_shared;write();catalog.Load(path);
  assert(!catalog.UniqueVertexForPixel(reused_ps));
  data=verified_shared;
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
