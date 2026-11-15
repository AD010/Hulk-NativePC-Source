#include "../native_port/src/eor_spirv_bank.h"
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
  const auto p=std::filesystem::temp_directory_path()/"hulk_eor_shaderbank_synthetic.bin";
  auto data=Bank(2,2);Write(p,data);
  ShaderBank bank;bank.Load(p);
  assert(bank.size()==2 && bank.expected()==2 && bank.HasFullArchiveCoverage());
  Digest one{};one.fill(1);
  assert(bank.Find(one,Stage::Vertex));
  assert(!bank.Find(one,Stage::Pixel));
  Digest two{};two.fill(2);
  assert(bank.Find(two,Stage::Pixel) && bank.Find(two,Stage::Pixel)->spirv.size()==15);
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
