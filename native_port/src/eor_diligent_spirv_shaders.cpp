#include "eor_diligent_spirv_shaders.h"
#include "RenderDevice.h"
#include "Shader.h"
#include "RefCntAutoPtr.hpp"
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace hulk::eor::gpu {
struct DiligentAotShaderModules::State {
  explicit State(Diligent::IRenderDevice* p) : device{p} {}
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  std::map<std::pair<aot::Digest,aot::Stage>,
           Diligent::RefCntAutoPtr<Diligent::IShader>> modules;
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
  if(existing!=state_->modules.end())return existing->second.RawPtr();
  if(!bank.HasFullArchiveCoverage())
    throw std::logic_error("Partial original EoR shader bank cannot create native modules");
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
  desc.ByteCode=binary->spirv.data();
  desc.ByteCodeSize=binary->spirv.size()*sizeof(uint32_t);
  Diligent::RefCntAutoPtr<Diligent::IShader> shader;
  state_->device->CreateShader(desc,&shader);
  if(!shader)
    throw std::runtime_error("Vulkan rejected original EoR shader SPIR-V");
  auto [it,added]=state_->modules.emplace(key,std::move(shader));
  if(!added)throw std::logic_error("Duplicate native EoR shader cache key");
  return it->second.RawPtr();
}
std::size_t DiligentAotShaderModules::ModuleCount() const noexcept {
  return state_ ? state_->modules.size() : 0;
}
void DiligentAotShaderModules::Clear() noexcept {
  if(state_)state_->modules.clear();
}
} // namespace hulk::eor::gpu
