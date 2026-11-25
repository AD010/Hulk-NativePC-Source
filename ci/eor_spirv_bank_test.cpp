#include "../native_port/src/eor_spirv_bank.h"
#include "../native_port/src/eor_spirv_point_size.h"
#include "../native_port/src/eor_spirv_static_registers.h"
#include "../native_port/src/eor_spirv_clip_position.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::aot;
void Dword(std::vector<uint8_t>& out, uint32_t n) {
  for (unsigned i=0; i<4; ++i) out.push_back(uint8_t(n>>(i*8)));
}
void Record(std::vector<uint8_t>& out, uint8_t key, uint32_t stage) {
  for (unsigned i=0;i<32;++i)out.push_back(key);
  const std::vector<uint32_t> words{
      0x07230203u, 0x10500u, 1u, 12u, 0u,       // module header
      0x00020011u, 1u,                            // OpCapability Shader
      0x0003000eu, 0u, 1u,                        // OpMemoryModel
      0x0005000fu, stage==1?0u:4u, 1u, 0x6e69616du, 0u // OpEntryPoint "main"
  };
  Dword(out,stage);Dword(out,static_cast<uint32_t>(words.size()));
  for(uint32_t value:words)Dword(out,value);
}
std::vector<uint8_t> Bank(uint32_t expected,uint32_t compiled) {
  std::vector<uint8_t> out{'H','E','O','R','S','P','V','1'};
  Dword(out,expected);Dword(out,compiled);
  for(uint32_t i=0;i<compiled;++i) Record(out,uint8_t(i+1),i%2?2:1);
  return out;
}
void Write(const std::filesystem::path& p,const std::vector<uint8_t>& bytes){
  std::ofstream file(p,std::ios::binary|std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}
void Bad(std::function<void()> f) {bool threw=false;try{f();}catch(const std::invalid_argument&){threw=true;} assert(threw);}
int main() {
  {
    // Minimal synthetic instruction stream, no copyrighted program.
    std::vector<uint32_t> point{0x07230203u,0x10000u,0,16,0,
        (4u<<16)|15u,0,1,0,
        (4u<<16)|71u,5,11,1,
        (4u<<16)|59u,2,5,3,
        (3u<<16)|62u,5,6};
    assert(HasOriginalPointSizeOutput(point));
    point.back()=7;assert(HasOriginalPointSizeOutput(point));
    point[point.size()-2]=4;assert(!HasOriginalPointSizeOutput(point));
    point[point.size()-2]=5;point[16]=1;assert(!HasOriginalPointSizeOutput(point));
    point[16]=3;point.pop_back();assert(!HasOriginalPointSizeOutput(point));
  }
  const auto p=std::filesystem::temp_directory_path()/"hulk_eor_shaderbank_synthetic.bin";
  auto data=Bank(2,2);Write(p,data);
  ShaderBank bank;bank.Load(p);
  assert(bank.size()==2 && bank.expected()==2 && bank.HasFullArchiveCoverage());
  Digest one{};one.fill(1);
  const auto* vertex=bank.Find(one,Stage::Vertex);
  assert(vertex && vertex->scalar_register_reads.has_value());
  assert(*vertex->scalar_register_reads==AnalyzeScalarRegisterReads(vertex->spirv));
  assert(vertex->scalar_preserves_clip_xyw.has_value());
  assert(*vertex->scalar_preserves_clip_xyw==ScalarPreservesClipXYW(vertex->spirv));
  assert(!bank.Find(one,Stage::Pixel));
  Digest two{};two.fill(2);
  assert(bank.Find(two,Stage::Pixel) && bank.Find(two,Stage::Pixel)->spirv.size()==15);
  assert(!bank.Find(two,Stage::Pixel)->scalar_register_reads.has_value());
  // A new successful load derives its own evidence; no SHA/address cache
  // survives an AOT generation or trusts caller-provided mutable bytecode.
  // First OpCapability word starts at the first record's 5-word header end.
  // Changing its wordcount to zero must be rejected before any Vulkan upload.
  auto damaged=data;
  damaged[16+40+5*4+2]=0;
  damaged[16+40+5*4+3]=0;
  Write(p,damaged);Bad([&]{bank.Load(p);});
  assert(bank.size()==2 && bank.HasFullArchiveCoverage());
  // A structurally well-formed fragment entry point cannot substitute a VS.
  damaged=Bank(2,2);
  damaged[16+40+11*4]=4; // first entry's execution model: Fragment, not Vertex
  Write(p,damaged);Bad([&]{bank.Load(p);});
  assert(bank.Find(one,Stage::Vertex));
  // The Vulkan module loader explicitly requests EntryPoint="main".
  // A different entry name or a hidden NUL in interface IDs must fail
  // source preflight, before any driver PSO compilation.
  damaged=Bank(2,2);
  damaged[16+40+13*4]='x'; // first entry name "main" -> "xain"
  Write(p,damaged);Bad([&]{bank.Load(p);});
  damaged=Bank(2,2);
  damaged[16+40+14*4]=1; // missing exact trailing NUL word
  Write(p,damaged);Bad([&]{bank.Load(p);});
  // Duplicate OpEntryPoint main in one module is ambiguous even if stage
  // matches and the driver could otherwise choose one arbitrarily.
  damaged=Bank(1,1);
  damaged[16+36]=20; // first module word count: 15 -> 20
  for(uint32_t value:{0x0005000fu,0u,1u,0x6e69616du,0u})
    Dword(damaged,value);
  Write(p,damaged);Bad([&]{bank.Load(p);});
  damaged=Bank(2,2);damaged.pop_back();Write(p,damaged);
  Bad([&]{bank.Load(p);});assert(bank.size()==2);
  damaged=Bank(2,2);damaged[0]='X';Write(p,damaged);Bad([&]{bank.Load(p);});
  damaged=Bank(1,2);Write(p,damaged);Bad([&]{bank.Load(p);});
  damaged=Bank(2,2);
  for(unsigned i=0;i<32;++i)damaged[16+100+i]=1; // second SHA duplicates first
  Write(p,damaged);Bad([&]{bank.Load(p);});
  damaged=Bank(3,2);Write(p,damaged);bank.Load(p);
  assert(bank.size()==2 && !bank.HasFullArchiveCoverage());
  std::filesystem::remove(p);
}
