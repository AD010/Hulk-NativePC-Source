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
         bindings[0].descriptor_set==0 && bindings[0].binding==2 &&
         bindings[0].spirv_dim==1 && !bindings[0].image_arrayed &&
         !bindings[0].image_multisampled);
  auto canonical=shader;
  canonical.resize(canonical.size()-named.size()-1);
  auto fixed_name=Text("HulkTexture2D_s2");
  std::vector<uint32_t> fixed_var{4};
  fixed_var.insert(fixed_var.end(),fixed_name.begin(),fixed_name.end());
  Add(canonical,5,fixed_var);
  assert(InspectSpirvImageResources(canonical,Stage::Vertex).size()==1);
  Reject([&]{(void)InspectSpirvImageResources(canonical,Stage::Pixel);});
  auto wrong_shape=canonical;wrong_shape[8]=3;
  Reject([&]{(void)InspectSpirvImageResources(wrong_shape);});
  assert(DecodeFixedAotImageSlot("HulkTextureCube_s15")->binding==47);
  assert(!DecodeFixedAotImageSlot("g_Diffuse"));
  for(const char* name:{"HulkTexture2D_s","HulkTexture2D_s01","HulkTexture2D_s16"})
    Reject([&]{(void)DecodeFixedAotImageSlot(name);});
  // NativeX-like source texture is a 2D array, despite one array layer.
  auto arrayed=shader;
  // OpTypeImage Arrayed operand is 5 after the type instruction word.
  arrayed[5+5]=1;
  const auto array_bindings=InspectSpirvImageResources(arrayed);
  assert(array_bindings.size()==1 &&
         array_bindings[0].spirv_dim==1 &&
         array_bindings[0].image_arrayed &&
         !array_bindings[0].image_multisampled);
  auto multisample=shader;multisample[5+6]=1;
  assert(InspectSpirvImageResources(multisample)[0].image_multisampled);
  // Separate sampler descriptors must not collide with image descriptors.
  auto sampler=shader;
  Add(sampler,26,{6});
  Add(sampler,32,{7,0,6});
  Add(sampler,59,{7,8,0});
  Add(sampler,71,{8,33,5});
  Add(sampler,71,{8,34,0});
  Reject([&]{(void)InspectSpirvImageResources(sampler);});
  const auto sampler_name=Text("HulkSampler_s3");
  std::vector<uint32_t> sampler_var{8};
  sampler_var.insert(sampler_var.end(),sampler_name.begin(),sampler_name.end());
  Add(sampler,5,sampler_var);
  assert(InspectSpirvImageResources(sampler).size()==1);
  auto collision=sampler;
  for(size_t at=5;at<collision.size();at+=collision[at]>>16)
    if((collision[at]&65535u)==71u && collision[at+1]==8 && collision[at+2]==33)
      collision[at+3]=2;
  Reject([&]{(void)InspectSpirvImageResources(collision);});
  // The original material/SRB path addresses image variables by their
  // exact names. A second descriptor with the same name is ambiguous even
  // when the set and binding decorations are distinct.
  auto duplicate_name=shader;
  Add(duplicate_name,59,{3,5,0});
  Add(duplicate_name,71,{5,33,3});
  Add(duplicate_name,71,{5,34,0});
  std::vector<uint32_t> alias_name{5};
  alias_name.insert(alias_name.end(),text.begin(),text.end());
  Add(duplicate_name,5,alias_name);
  Reject([&]{(void)InspectSpirvImageResources(duplicate_name);});
  // An image descriptor array cannot be represented by a single Vulkan SRV
  // without verifying its original array cardinality and per-index ownership.
  auto array_descriptor=shader;
  Add(array_descriptor,21,{6,32,0}); // uint32 type
  Add(array_descriptor,43,{6,7,2});  // length constant 2
  Add(array_descriptor,28,{8,1,7});  // array[2] of Image
  Add(array_descriptor,32,{9,0,8});  // UniformConstant pointer to image array
  Add(array_descriptor,59,{9,10,0});
  Add(array_descriptor,71,{10,33,3});
  Add(array_descriptor,71,{10,34,0});
  std::vector<uint32_t> arr_name{10};
  const auto arr_text=Text("g_Layers");
  arr_name.insert(arr_name.end(),arr_text.begin(),arr_text.end());
  Add(array_descriptor,5,arr_name);
  Reject([&]{(void)InspectSpirvImageResources(array_descriptor);});
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
