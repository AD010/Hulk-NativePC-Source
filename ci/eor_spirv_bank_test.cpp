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
  Dword(out,stage);Dword(out,6);
  for(uint32_t value:{0x07230203u,0x10500u,1u,12u,0u,0u})Dword(out,value);
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
  assert(bank.Find(two,Stage::Pixel) && bank.Find(two,Stage::Pixel)->spirv.size()==6);
  auto damaged=data;damaged.back()=0x33;Write(p,damaged);
  // Tail mutation affects opcode, not SPIR-V header: basic loader does not
  // claim a full spirv-val validator or binary SHA verification.
  bank.Load(p);assert(bank.size()==2);
  damaged=Bank(2,2);damaged.pop_back();Write(p,damaged);
  Bad([&]{bank.Load(p);});assert(bank.size()==2);
  damaged=Bank(2,2);damaged[0]='X';Write(p,damaged);Bad([&]{bank.Load(p);});
  damaged=Bank(1,2);Write(p,damaged);Bad([&]{bank.Load(p);});
  damaged=Bank(2,2);
  for(unsigned i=0;i<32;++i)damaged[16+64+i]=1; // second SHA duplicates first
  Write(p,damaged);Bad([&]{bank.Load(p);});
  damaged=Bank(3,2);Write(p,damaged);bank.Load(p);
  assert(bank.size()==2 && !bank.HasFullArchiveCoverage());
  std::filesystem::remove(p);
}
