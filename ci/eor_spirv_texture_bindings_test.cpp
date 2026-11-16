#include "../native_port/src/eor_spirv_texture_bindings.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor::aot;
namespace {
void Add(std::vector<uint32_t>& s,uint16_t op,std::vector<uint32_t> fields) {
  s.push_back((static_cast<uint32_t>(fields.size()+1)<<16)|op);
  s.insert(s.end(),fields.begin(),fields.end());
}
std::vector<uint32_t> Text(const std::string& name) {
  std::vector<uint32_t> out;
  std::string zero=name+'\0';
  for(size_t i=0;i<zero.size();i+=4) {
    uint32_t word=0;
    for(size_t j=0;j<4 && i+j<zero.size();++j)
      word|=uint32_t(static_cast<uint8_t>(zero[i+j]))<<(8*j);
    out.push_back(word);
  }
  return out;
}
template<typename F> void Reject(F f) {
  bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
}
}
int main() {
  std::vector<uint32_t> shader{0x07230203,0x00010500,0,16,0};
  Add(shader,25,{1,2,1,0,0,0,1,0}); // OpTypeImage
  Add(shader,32,{3,0,1});           // UniformConstant ptr to image.
  Add(shader,59,{3,4,0});           // OpVariable.
  Add(shader,71,{4,33,2});         // Binding 2.
  Add(shader,71,{4,34,0});         // Set 0.
  auto text=Text("g_Diffuse");
  std::vector<uint32_t> named{4};
  named.insert(named.end(),text.begin(),text.end());
  Add(shader,5,named);
  const auto bindings=InspectSpirvImageResources(shader);
  assert(bindings.size()==1 && bindings[0].exact_variable_name=="g_Diffuse" &&
         bindings[0].descriptor_set==0 && bindings[0].binding==2);
  auto bad=shader;bad[0]=0;
  Reject([&]{(void)InspectSpirvImageResources(bad);});
  bad=shader;bad[5]=0;
  Reject([&]{(void)InspectSpirvImageResources(bad);});
  bad=shader;
  // Missing OpDecorate Binding 2 -> no legal shader image source.
  bad.erase(bad.begin()+22,bad.begin()+26);
  Reject([&]{(void)InspectSpirvImageResources(bad);});
  // Shader with no UniformConstant texture declarations is valid.
  std::vector<uint32_t> empty{0x07230203,0x00010500,0,16,0};
  Add(empty,17,{1}); // OpCapability Shader.
  assert(InspectSpirvImageResources(empty).empty());
}
