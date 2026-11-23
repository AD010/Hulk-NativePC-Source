#include "eor_diligent_original_pipeline.h"
#include "RenderDevice.h"
#include "ShaderResourceBinding.h"
#include "Shader.h"
#include <stdexcept>
namespace hulk::eor::gpu {
OriginalVulkanPipeline CreateOriginalVulkanPipeline(
    Diligent::IRenderDevice* device,
    DiligentAotShaderModules& shaders,
    const aot::ShaderBank& bank,
    const aot::EffectCatalog& original_effects,
    const VerifiedOriginalGraphicsPass& pass) {
  if(!device || device->GetDeviceInfo().Type != Diligent::RENDER_DEVICE_TYPE_VULKAN ||
     !pass.original_effect_hash || pass.original_pass_index==UINT32_MAX ||
     !pass.material_pass_verified || !pass.original_render_state_verified ||
     !pass.original_host_vertex_layout_verified ||
     pass.exact_host_input_layout.empty() ||
     pass.exact_host_input_layout.size()>DILIGENT_MAX_LAYOUT_ELEMENTS)
    throw std::invalid_argument("Original EoR Vulkan pass ownership/vertex layout unresolved");
  // Only this exact proven shader pair must be compiled: other original
  // effects must not block the first real independent native Vulkan scene.
  if(!bank.size() ||
     original_effects.ShaderCount()!=bank.expected() ||
     !original_effects.HasPair(pass.original_effect_hash,pass.original_pass_index,
                               pass.original_vertex_shader_sha256,
                               pass.original_pixel_shader_sha256))
    throw std::invalid_argument("Original EoR Vulkan PSO has no verified effect/shader pairing");
  const auto& state=pass.exact_render_state;
  if(state.NumRenderTargets==0 || state.NumRenderTargets>DILIGENT_MAX_RENDER_TARGETS ||
     (pass.original_depth_unbound!=(state.DSVFormat==Diligent::TEX_FORMAT_UNKNOWN)) ||
     (pass.original_depth_unbound &&
      (state.DepthStencilDesc.DepthEnable || state.DepthStencilDesc.DepthWriteEnable ||
       state.DepthStencilDesc.StencilEnable)) ||
     (state.SmplDesc.Count!=1 && state.SmplDesc.Count!=2 &&
      state.SmplDesc.Count!=4 && state.SmplDesc.Count!=8) ||
     state.PrimitiveTopology!=Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    throw std::invalid_argument("Original EoR color/depth/MSAA/primitive contract unverified");
  for(uint32_t i=0;i<state.NumRenderTargets;++i)
    if(state.RTVFormats[i]==Diligent::TEX_FORMAT_UNKNOWN)
      throw std::invalid_argument("Original EoR pass render-target format unknown");
  // Explicitly reject input layout indices the current GPU API cannot use.
  for(const auto& a:pass.exact_host_input_layout)
    if(a.NumComponents<1 || a.NumComponents>4 || a.BufferSlot>15 ||
       a.ValueType==Diligent::VT_UNDEFINED)
      throw std::invalid_argument("Original EoR host vertex semantic invalid");
  auto* vertex=shaders.GetOrCreate(bank,pass.original_vertex_shader_sha256,aot::Stage::Vertex);
  auto* pixel=shaders.GetOrCreate(bank,pass.original_pixel_shader_sha256,aot::Stage::Pixel);
  if(!vertex || !pixel)
    throw std::logic_error("Original EoR Vulkan shader module missing");
  Diligent::GraphicsPipelineStateCreateInfo ci{};
  ci.PSODesc.Name="EoR original source-verified Vulkan effect";
  ci.PSODesc.PipelineType=Diligent::PIPELINE_TYPE_GRAPHICS;
  ci.PSODesc.ResourceLayout.DefaultVariableType=
      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
  ci.pVS=vertex;
  ci.pPS=pixel;
  ci.GraphicsPipeline=state;
  // XDK always clips against the source viewport. MapOriginalRasterWindow
  // additionally intersects the source scissor only when the game enables it.
  ci.GraphicsPipeline.RasterizerDesc.ScissorEnable=true;
  // Font attributes are uploaded in separate arrays of float4. An
  // automatic stride for FLOAT3/FLOAT2 would advance by 12/8 bytes on
  // Vulkan, fetching part of the preceding glyph vertex on every step.
  auto layout=pass.exact_host_input_layout;
  if(pass.original_dynamic_font_vertex_layout_verified) {
    for(auto& element:layout) {
      if(element.Stride!=Diligent::LAYOUT_ELEMENT_AUTO_STRIDE && element.Stride!=16)
        throw std::invalid_argument("EFont upload requires a float4 stride");
      element.Stride=16;
    }
  }
  ci.GraphicsPipeline.InputLayout.NumElements=
      static_cast<Diligent::Uint32>(pass.exact_host_input_layout.size());
  ci.GraphicsPipeline.InputLayout.LayoutElements=layout.data();
  OriginalVulkanPipeline out{};
  device->CreateGraphicsPipelineState(ci,&out.pipeline);
  if(!out.pipeline)
    throw std::runtime_error("Vulkan rejected original EoR source-verified PSO");
  out.pipeline->CreateShaderResourceBinding(&out.bindings, true);
  if(!out.bindings)
    throw std::runtime_error("Vulkan rejected original EoR PSO shader resource layout");
  return out;
}
} // namespace hulk::eor::gpu
