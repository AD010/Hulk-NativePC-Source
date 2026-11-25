#include "eor_spirv_texture_bindings.h"
#include "eor_spirv_literal_string.h"
#include <map>
#include <charconv>
#include <set>
#include <stdexcept>
#include <utility>
namespace hulk::eor::aot {
namespace {
enum class TypeKind { Other, Image, SampledImage, Sampler, Alias, Array };
struct Type {
  TypeKind kind=TypeKind::Other;
  uint32_t base=0;
  uint32_t dim=UINT32_MAX;
  bool arrayed=false;
  bool multisampled=false;
};
struct Variable { uint32_t pointer=0;uint32_t id=0; };
std::string Name(std::span<const uint32_t> words) {
  auto result=DecodeTerminalSpirvString(words);
  if(!result || result->empty() || result->size()>128)
    throw std::invalid_argument("Unnamed or invalid original SPIR-V image resource");
  for(unsigned char c:*result)
    if(c<0x21 || c>0x7e || c=='/' || c=='\\')
      throw std::invalid_argument("Unsupported original SPIR-V image resource variable name");
  return std::move(*result);
}
const Type* FindSampledResourceType(uint32_t id, const std::map<uint32_t,Type>& types) {
  for(unsigned depth=0;depth<16;++depth) {
    const auto it=types.find(id);
    if(it==types.end())return nullptr;
    if(it->second.kind==TypeKind::Image || it->second.kind==TypeKind::Sampler)return &it->second;
    if(it->second.kind==TypeKind::Array)
      throw std::invalid_argument(
          "Original AOT shader uses an array of GPU image descriptors without verified array bindings");
    if(it->second.kind!=TypeKind::Alias &&
       it->second.kind!=TypeKind::SampledImage)return nullptr;
    id=it->second.base;
  }
  throw std::invalid_argument("Cyclic/deep original shader image type chain");
}
}
std::optional<FixedAotImageSlot> DecodeFixedAotImageSlot(std::string_view name) {
  if(!name.starts_with("HulkTexture"))return {};
  name.remove_prefix(11);
  struct Shape {std::string_view prefix;uint32_t dim,base;};
  constexpr Shape shapes[]={{"1D_s",0,64},{"2D_s",1,0},{"3D_s",2,16},{"Cube_s",3,32}};
  for(const auto& shape:shapes) {
    if(!name.starts_with(shape.prefix))continue;
    name.remove_prefix(shape.prefix.size());
    uint32_t slot=0;
    const auto end=name.data()+name.size();
    const auto parsed=std::from_chars(name.data(),end,slot);
    if(parsed.ec==std::errc{} && parsed.ptr==end && slot<16 &&
       !(name.size()>1 && name.front()=='0'))
      return FixedAotImageSlot{slot,shape.dim,shape.base+slot};
    break;
  }
  throw std::invalid_argument("Invalid scalar AOT image slot");
}
std::vector<SpirvImageResource> InspectSpirvImageResources(
    std::span<const uint32_t> shader,std::optional<Stage> stage) {
  if(shader.size()<5 || shader.size()>(8u<<20)/4u ||
     shader[0]!=0x07230203u ||
     shader[3]==0 || shader[3]>1000000 ||
     shader[4]!=0)
    throw std::invalid_argument("Original Xenos AOT shader is not valid SPIR-V");
  std::map<uint32_t,Type> types;
  std::map<uint32_t,std::span<const uint32_t>> names;
  std::map<uint32_t,uint32_t> sets,bindings;
  std::vector<Variable> vars;
  std::set<uint32_t> variable_ids;
  for(size_t pos=5;pos<shader.size();) {
    const uint32_t instruction=shader[pos];
    const size_t words=instruction>>16;
    const uint32_t op=instruction&0xffffu;
    if(words==0 || words>shader.size()-pos)
      throw std::invalid_argument("Malformed original Xenos SPIR-V instruction span");
    const auto operand=[&](size_t i) -> uint32_t {
      if(i>=words)throw std::invalid_argument("Truncated original SPIR-V resource declaration");
      return shader[pos+i];
    };
    if(op==25 || op==26 || op==27 || op==28 || op==29 || op==32) {
      const size_t required=op==25?9u:op==26?2u:op==27||op==29?3u:4u;
      if(words!=required && !(op==25 && words==10))
        throw std::invalid_argument("Invalid SPIR-V resource type declaration length");
      const auto id=operand(1);
      if(!id || id>=shader[3] || types.contains(id))
        throw std::invalid_argument("Duplicate/unbounded original SPIR-V type id");
      if(op==25) {
        // Image type declarations must carry Dim, Arrayed, and MS;
        // never infer a 2D view from the material texture dimensions.
        if(words<9 || operand(5)>1 || operand(6)>1)
          throw std::invalid_argument("Malformed original SPIR-V image shape");
        types.emplace(id,Type{TypeKind::Image,0,
                    operand(3),operand(5)!=0,operand(6)!=0});
      } else if(op==26) types.emplace(id,Type{TypeKind::Sampler,0});
      else if(op==27 || op==28 || op==29) {
        types.emplace(id,Type{op==27?TypeKind::SampledImage:TypeKind::Array,operand(2)});
      } else { // OpTypePointer = (result, storage class, pointee).
        (void)operand(2);
        types.emplace(id,Type{TypeKind::Alias,operand(3)});
      }
    } else if(op==59) { // OpVariable: result type, result id, storage class.
      const uint32_t ptr=operand(1),id=operand(2),storage=operand(3);
      if((words!=4 && words!=5) || !ptr || ptr>=shader[3] ||
         !id || id>=shader[3] || !variable_ids.insert(id).second)
        throw std::invalid_argument("Invalid or duplicate SPIR-V variable ID/length");
      if(storage==0)vars.push_back({ptr,id}); // UniformConstant.
    } else if(op==5) { // OpName: target ID, NUL-terminated ASCII.
      const auto id=operand(1);
      if(!id || id>=shader[3] || words<3 || names.contains(id))
        throw std::invalid_argument("Bad SPIR-V original resource name ID");
      names.emplace(id,shader.subspan(pos+2,words-2));
    } else if(op==71) { // OpDecorate: target, decoration, literal.
      const auto id=operand(1),kind=operand(2);
      if(kind==33 || kind==34) {
        const uint32_t value=operand(3);
        if(words!=4 || !id || id>=shader[3] ||
           !(kind==33?bindings:sets).emplace(id,value).second)
          throw std::invalid_argument("Duplicate/invalid SPIR-V descriptor decoration");
      }
    }
    pos+=words;
  }
  std::vector<SpirvImageResource> result;
  std::set<std::pair<uint32_t,uint32_t>> occupied;
  std::set<std::string> unique_resource_names;
  for(const auto& variable:vars) {
    const auto* resource=FindSampledResourceType(variable.pointer,types);
    if(!resource)continue;
    const auto name=names.find(variable.id);
    const auto set=sets.find(variable.id);
    const auto bind=bindings.find(variable.id);
    if(name==names.end() || set==sets.end() || bind==bindings.end() ||
       !occupied.emplace(set->second,bind->second).second)
      throw std::invalid_argument("Original AOT SPIR-V image descriptor lacks unique binding/name");
    // Diligent resolves a texture variable by NAME at the verified stage.
    // Two distinct image descriptors with the same name can bind the wrong
    // material even if their SPIR-V set/binding numbers differ.
    auto exact_name=Name(name->second);
    // Separate sampler descriptors share the same Vulkan binding namespace
    // as images. Validate them without returning them as texture SRVs.
    if(!unique_resource_names.insert(exact_name).second)
      throw std::invalid_argument("Ambiguous duplicated original SPIR-V resource variable name");
    if(resource->kind==TypeKind::Sampler)continue;
    if(const auto fixed=DecodeFixedAotImageSlot(exact_name)) {
      if(resource->dim!=fixed->dimension || resource->arrayed || resource->multisampled ||
         bind->second!=fixed->binding || set->second>1 ||
         (stage && set->second!=(*stage==Stage::Pixel ? 1u : 0u)))
        throw std::invalid_argument("Scalar AOT image differs from original stage, dimension or binding");
    }
    result.push_back({std::move(exact_name),set->second,bind->second,
                      resource->dim,resource->arrayed,resource->multisampled});
  }
  return result;
}
} // namespace hulk::eor::aot
