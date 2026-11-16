#pragma once
// Direct host Vulkan PSO for a VERIFIED EoR material effect. Xenos PM4, EDRAM,
// guest register snapshots and speculative shader states are not consulted.
#include "eor_diligent_spirv_shaders.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include <cstdint>
#include <vector>
namespace Diligent {
struct IRenderDevice;
struct IPipelineState;
struct IShaderResourceBinding;
}
namespace hulk::eor::gpu {
struct VerifiedOriginalGraphicsPass {
  uint32_t original_effect_hash=0;
  uint32_t original_pass_index=UINT32_MAX;
  aot::Digest original_vertex_shader_sha256{};
  aot::Digest original_pixel_shader_sha256{};
  Diligent::GraphicsPipelineDesc exact_render_state{};
  std::vector<Diligent::LayoutElement> exact_host_input_layout;
  bool material_pass_verified=false;
  bool original_render_state_verified=false;
  bool original_host_vertex_layout_verified=false;
};
struct OriginalVulkanPipeline {
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> bindings;
};
// Creates a real SPIR-V-backed Vulkan PSO and SRB, only after the calling
// engine bridge supplies a source-proven pass, color/depth formats, MSAA,
// and a vertex layout. No guessed blend/depth/culling/sampler state.
OriginalVulkanPipeline CreateOriginalVulkanPipeline(
    Diligent::IRenderDevice* device,
    DiligentAotShaderModules& native_shaders,
    const aot::ShaderBank& bank,
    const VerifiedOriginalGraphicsPass& verified);
} // namespace hulk::eor::gpu
