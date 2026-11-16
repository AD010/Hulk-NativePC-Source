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
}

namespace hulk::eor::gpu {
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
                                aot::Stage stage);
  std::size_t ModuleCount() const noexcept;
  void Clear() noexcept; // only after GPU completion fence
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace hulk::eor::gpu
