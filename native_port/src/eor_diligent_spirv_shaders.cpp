#include "eor_diligent_spirv_shaders.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "ShaderResourceBinding.h"
#include "Buffer.h"
#include <cstring>
#include <string_view>
#include "Shader.h"
#include "RefCntAutoPtr.hpp"
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace hulk::eor::gpu {
namespace {
constexpr size_t kRegisterFileBytes=256*4*sizeof(float);
const char* FloatBufferName(aot::Stage stage) {
  return stage==aot::Stage::Vertex ? "HulkVertexConstants" : "HulkPixelConstants";
}
FloatRegisterAbi ReflectFloatAbi(Diligent::IShader& shader,aot::Stage stage) {
  using namespace Diligent;
  bool direct=false,external=false;
  for(Uint32 i=0;i<shader.GetResourceCount();++i) {
    ShaderResourceDesc resource{};
    shader.GetResourceDesc(i,resource);
    if(resource.Type!=SHADER_RESOURCE_TYPE_CONSTANT_BUFFER) {
      if(resource.Type!=SHADER_RESOURCE_TYPE_TEXTURE_SRV && resource.Type!=SHADER_RESOURCE_TYPE_SAMPLER)
        external=true;
      continue;
    }
    const std::string_view name=resource.Name ? resource.Name : "";
    if(name=="HulkSharedConstants")continue;
    if(name!=FloatBufferName(stage)) {
      external=true;
      continue;
    }
    auto* block=shader.GetConstantBufferDesc(i);
    if(direct || resource.ArraySize!=1 || !block || block->Size!=kRegisterFileBytes ||
       block->NumVariables!=1 || !block->pVariables)
      throw std::invalid_argument("Unexpected original stage float block");
    const auto& value=block->pVariables[0];
    const char* member=stage==aot::Stage::Vertex ? "HulkVertexFloats" : "HulkPixelFloats";
    if(!value.Name || std::string_view(value.Name)!=member || value.Offset!=0 ||
       value.ArraySize!=256 || value.Class!=SHADER_CODE_VARIABLE_CLASS_VECTOR ||
       value.BasicType!=SHADER_CODE_BASIC_TYPE_FLOAT || value.NumRows*value.NumColumns!=4 ||
       value.NumMembers)
      throw std::invalid_argument("Original stage register file must contain float4[256]");
    direct=true;
  }
  if(direct && external)throw std::invalid_argument("Ambiguous original shader constant ABI");
  return direct ? FloatRegisterAbi::Direct256 : external ? FloatRegisterAbi::External : FloatRegisterAbi::Unused;
}
} // namespace
struct DiligentAotShaderModules::State {
  explicit State(Diligent::IRenderDevice* p) : device{p} {}
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  struct Module { Diligent::RefCntAutoPtr<Diligent::IShader> shader; FloatRegisterAbi floats; };
  std::map<std::pair<aot::Digest,aot::Stage>,Module> modules;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> vertex_floats,pixel_floats;
};
DiligentAotShaderModules::DiligentAotShaderModules(Diligent::IRenderDevice* device)
    : state_(device ? std::make_unique<State>(device) : nullptr) {
  if(!device || device->GetDeviceInfo().Type != Diligent::RENDER_DEVICE_TYPE_VULKAN)
    throw std::invalid_argument("Original EoR SPIR-V shader modules require Vulkan");
}
DiligentAotShaderModules::~DiligentAotShaderModules() noexcept = default;
Diligent::IShader* DiligentAotShaderModules::GetOrCreate(
    const aot::ShaderBank& bank, const aot::Digest& digest, aot::Stage stage) {
  if(stage!=aot::Stage::Vertex && stage!=aot::Stage::Pixel)
    throw std::invalid_argument("Unsupported original EoR Vulkan shader stage");
  const auto key=std::make_pair(digest,stage);
  auto existing=state_->modules.find(key);
  if(existing!=state_->modules.end())return existing->second.shader.RawPtr();
  // A real native frame needs exactly its source-proven VS/PS, not all
  // 5194 unrelated original shader variants. ReplayAotScene verifies the
  // requested shader identities and effect/technique before BeginScene.
  // Never synthesize or substitute an absent original shader.
  const auto* binary=bank.Find(digest,stage);
  if(!binary || binary->spirv.size()<5 || binary->spirv.front()!=0x07230203u)
    throw std::invalid_argument("Original EoR shader identity missing or wrong stage");
  // ShaderBank::Load already checked SPIR-V header, instruction extents
  // and vertex/fragment entry points. The Vulkan driver still validates
  // full module semantics; a driver rejection does not change the cache.
  std::string name=stage==aot::Stage::Vertex ?
       "EoR original Vulkan VS" : "EoR original Vulkan PS";
  Diligent::ShaderCreateInfo desc;
  desc.Desc.Name=name.c_str();
  desc.Desc.ShaderType=stage==aot::Stage::Vertex ?
       Diligent::SHADER_TYPE_VERTEX : Diligent::SHADER_TYPE_PIXEL;
  desc.Desc.UseCombinedTextureSamplers=false;
  desc.EntryPoint="main";
  desc.LoadConstantBufferReflection=true;
  desc.ByteCode=binary->spirv.data();
  desc.ByteCodeSize=binary->spirv.size()*sizeof(uint32_t);
  Diligent::RefCntAutoPtr<Diligent::IShader> shader;
  state_->device->CreateShader(desc,&shader);
  if(!shader)
    throw std::runtime_error("Vulkan rejected original EoR shader SPIR-V");
  const auto abi=ReflectFloatAbi(*shader.RawPtr(),stage);
  auto& buffer=stage==aot::Stage::Vertex ? state_->vertex_floats : state_->pixel_floats;
  if(abi==FloatRegisterAbi::Direct256 && !buffer) {
    Diligent::BufferDesc descriptor{};
    descriptor.Name=FloatBufferName(stage);
    descriptor.BindFlags=Diligent::BIND_UNIFORM_BUFFER;
    descriptor.Size=kRegisterFileBytes;
    descriptor.Usage=Diligent::USAGE_DYNAMIC;
    descriptor.CPUAccessFlags=Diligent::CPU_ACCESS_WRITE;
    state_->device->CreateBuffer(descriptor,nullptr,&buffer);
    if(!buffer)throw std::runtime_error("Original stage uniform allocation failed");
  }
  auto [it,added]=state_->modules.emplace(key,State::Module{std::move(shader),abi});
  if(!added)throw std::logic_error("Duplicate native EoR shader cache key");
  return it->second.shader.RawPtr();
}
FloatRegisterAbi DiligentAotShaderModules::GetFloatRegisterAbi(
    const aot::ShaderBank& bank,const aot::Digest& digest,aot::Stage stage) {
  GetOrCreate(bank,digest,stage);
  return state_->modules.at({digest,stage}).floats;
}
void DiligentAotShaderModules::BindDirectFloatRegisters(Diligent::IDeviceContext* context,
    Diligent::IShaderResourceBinding* resources,aot::Stage stage,std::span<const float> registers) {
  using namespace Diligent;
  if(!context || !resources || (stage!=aot::Stage::Vertex && stage!=aot::Stage::Pixel) ||
     registers.size_bytes()!=kRegisterFileBytes)
    throw std::invalid_argument("Incomplete captured shader register file");
  auto* buffer=(stage==aot::Stage::Vertex ? state_->vertex_floats : state_->pixel_floats).RawPtr();
  auto* var=resources->GetVariableByName(
      stage==aot::Stage::Vertex ? SHADER_TYPE_VERTEX : SHADER_TYPE_PIXEL,FloatBufferName(stage));
  if(!buffer || !var)throw std::invalid_argument("Missing original float buffer binding");
  void* mapped=nullptr;
  context->MapBuffer(buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);
  if(!mapped)throw std::runtime_error("Could not map original float register file");
  std::memcpy(mapped,registers.data(),registers.size_bytes());
  context->UnmapBuffer(buffer,MAP_WRITE);
  if(var->Get()!=buffer)var->Set(buffer,SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
}
std::size_t DiligentAotShaderModules::ModuleCount() const noexcept {
  return state_ ? state_->modules.size() : 0;
}
void DiligentAotShaderModules::Clear() noexcept {
  if(state_) {
    state_->modules.clear();
    state_->vertex_floats.Release();
    state_->pixel_floats.Release();
  }
}
} // namespace hulk::eor::gpu
