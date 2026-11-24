#pragma once
// Native EoR AOT shader modules: original Xenos shader SHA -> immutable
// host Vulkan SPIR-V module. Does not translate PM4, emulate EDRAM, invent
// fixed-function state or compile arbitrary guest GPU commands at runtime.
#include "eor_spirv_bank.h"
#include <cstddef>
#include <memory>

namespace Diligent {
struct IRenderDevice;
struct IShader;
struct IDeviceContext;
struct IShaderResourceBinding;
}

namespace hulk::eor::gpu {
enum class FloatRegisterAbi { External, Unused, Direct256 };
class DiligentAotShaderModules final {
 public:
  // Caller owns the Vulkan render thread; this cache keeps its own device ref.
  explicit DiligentAotShaderModules(Diligent::IRenderDevice* device);
  DiligentAotShaderModules(const DiligentAotShaderModules&) = delete;
  DiligentAotShaderModules& operator=(const DiligentAotShaderModules&) = delete;
  ~DiligentAotShaderModules() noexcept;

  // Returns a native GPU shader compiled from the *verified* owner's
  // original SPIR-V bank. Reused effects share the same shader object.
  // Failure never creates fallback, magenta or placeholder shaders.
  Diligent::IShader* GetOrCreate(const aot::ShaderBank& bank,
                                const aot::Digest& original_source_sha256,
                                aot::Stage stage,uint16_t xbox_cube_gamma_mask=0);
  FloatRegisterAbi GetFloatRegisterAbi(const aot::ShaderBank& bank,
      const aot::Digest& digest,aot::Stage stage);
  // Immediately before CommitShaderResources, rename the stage's dynamic
  // uniform allocation. Earlier draws retain their previous GPU contents.
  void BindDirectFloatRegisters(Diligent::IDeviceContext* context,
      Diligent::IShaderResourceBinding* resources,aot::Stage stage,
      std::span<const float> registers);
  std::size_t ModuleCount() const noexcept;
  void Clear() noexcept; // only after GPU completion fence
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::gpu
