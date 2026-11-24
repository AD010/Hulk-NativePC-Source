#include "eor_diligent_original_pipeline.h"
#include "RenderDevice.h"
#include "ShaderResourceBinding.h"
#include "Shader.h"
#include <stdexcept>
#include <array>
namespace hulk::eor::gpu {
namespace {
// Read the original device shadow using the pinned USA XDK register group
// index, not the source's linear offset treated as a GPU register address.
std::optional<Diligent::COMPARISON_FUNCTION> OriginalCompare(
    uint32_t encoded) noexcept {
  using namespace Diligent;
  constexpr std::array<COMPARISON_FUNCTION,8> values{
      COMPARISON_FUNC_NEVER,COMPARISON_FUNC_LESS,COMPARISON_FUNC_EQUAL,
      COMPARISON_FUNC_LESS_EQUAL,COMPARISON_FUNC_GREATER,
      COMPARISON_FUNC_NOT_EQUAL,COMPARISON_FUNC_GREATER_EQUAL,
      COMPARISON_FUNC_ALWAYS};
  return encoded<values.size()?std::optional{values[encoded]}:std::nullopt;
}
std::optional<Diligent::BLEND_FACTOR> OriginalBlend(uint32_t encoded,
                                                   bool alpha) noexcept {
  using namespace Diligent;
  switch(encoded) {
    case 0:return BLEND_FACTOR_ZERO;
    case 1:return BLEND_FACTOR_ONE;
    case 4:if(!alpha)return BLEND_FACTOR_SRC_COLOR;break;
    case 5:if(!alpha)return BLEND_FACTOR_INV_SRC_COLOR;break;
    case 6:return BLEND_FACTOR_SRC_ALPHA;
    case 7:return BLEND_FACTOR_INV_SRC_ALPHA;
    case 8:if(!alpha)return BLEND_FACTOR_DEST_COLOR;break;
    case 9:if(!alpha)return BLEND_FACTOR_INV_DEST_COLOR;break;
    case 10:return BLEND_FACTOR_DEST_ALPHA;
    case 11:return BLEND_FACTOR_INV_DEST_ALPHA;
    case 16:if(!alpha)return BLEND_FACTOR_SRC_ALPHA_SAT;break;
    default:break; // Xenos blend constants require an independent source value
  }
  return {};
}
std::optional<Diligent::BLEND_OPERATION> OriginalBlendOp(
    uint32_t encoded) noexcept {
  using namespace Diligent;
  switch(encoded) {
    case 0:return BLEND_OPERATION_ADD;
    case 1:return BLEND_OPERATION_SUBTRACT;
    case 2:return BLEND_OPERATION_MIN;
    case 3:return BLEND_OPERATION_MAX;
    case 4:return BLEND_OPERATION_REV_SUBTRACT;
    default:return {};
  }
}
}
bool MatchesOriginalXdkGraphicsState(
    const VerifiedOriginalGraphicsPass& original,
    const xdk::OriginalXdkFixedGraphicsState& source) noexcept {
  if(!original.material_pass_verified ||
     !original.original_render_state_verified ||
     !source.original_guest_device)
    return false;
  const auto read=[&](uint32_t index)->std::optional<uint32_t> {
    return source.ReadOriginalXenosRegister(index);
  };
  const auto depth=read(0x2200),blend=read(0x2201),
             color=read(0x2202),raster=read(0x2205),
             color_mask=read(0x2104),surface=read(0x2000),
             clip=read(0x2204);
  if(!depth || !blend || !color || !raster || !color_mask ||
     !surface || !clip)return false;
  const auto& state=original.exact_render_state;
  const auto& ds=state.DepthStencilDesc;
  const auto& rs=state.RasterizerDesc;
  const auto& bs=state.BlendDesc;
  // Alpha test must be done by the original translated pixel shader;
  // alpha-to-coverage dithering, source clip planes, polygon modes and
  // source depth bias are not interchangeable with a host PSO default.
  // Xenos polygon mode 0 draws filled triangles. Mode 1 selects
  // per-face primitive types and is compatible with host TRIANGLE_LIST
  // only when BOTH sides request triangles. Original Xbox titles also
  // use reserved value 2 to mean ordinary triangles (Xenos hardware
  // actually rasterizes them as filled, unlike the naïve bit-test that
  // rejected every such world Draw). Unknown mode 3 still fails closed.
  const uint32_t polygon_mode=(*raster>>3)&3u;
  const uint32_t polygon_front=(*raster>>5)&7u;
  const uint32_t polygon_back=(*raster>>8)&7u;
  if((*color&0x18u) || (*clip&0x3fu) ||
     polygon_mode==3u ||
     (polygon_mode==1u && (polygon_front!=2u || polygon_back!=2u)) ||
     (*raster&(7u<<11)) ||
     (*raster&(1u<<20)) || ((*raster&3u)==3u) ||
     (*depth&1u) || ((*surface>>16)&3u)>2u)
    return false;
  const uint32_t source_msaa=1u<<((*surface>>16)&3u);
  if(state.SmplDesc.Count!=source_msaa)return false;
  const bool z_enabled=(*depth&2u)!=0,z_write=(*depth&4u)!=0;
  if(ds.DepthEnable!=z_enabled ||
     ds.DepthWriteEnable!=(z_enabled && z_write) ||
     ds.StencilEnable || (original.original_depth_unbound && z_enabled))
    return false;
  const auto z_compare=OriginalCompare((*depth>>4)&7u);
  if(!z_compare || (z_enabled && ds.DepthFunc!=*z_compare))
    return false;
  const bool front=(*raster&1u)!=0,back=(*raster&2u)!=0;
  const auto cull=front ? Diligent::CULL_MODE_FRONT :
                  back ? Diligent::CULL_MODE_BACK : Diligent::CULL_MODE_NONE;
  if(rs.CullMode!=cull ||
     rs.FrontCounterClockwise!=((*raster&4u)==0u) ||
     rs.FillMode!=Diligent::FILL_MODE_SOLID)
    return false;
  if(bs.AlphaToCoverageEnable)return false;
  for(uint32_t i=0;i<state.NumRenderTargets;++i) {
    if(i>=4u)return false;
    const auto& target=bs.RenderTargets[i];
    const auto physical_mask=static_cast<Diligent::COLOR_MASK>(
        (*color_mask>>(i*4u))&15u);
    if(target.RenderTargetWriteMask!=physical_mask ||
       target.LogicOperationEnable)return false;
    if(!target.BlendEnable)continue;
    const uint32_t packed=i==0u?*blend:read(0x2208u+i).value_or(UINT32_MAX);
    if(packed==UINT32_MAX)return false;
    const auto src=OriginalBlend(packed&31u,false);
    const auto dst=OriginalBlend((packed>>8)&31u,false);
    const auto op=OriginalBlendOp((packed>>5)&7u);
    const auto asrc=OriginalBlend((packed>>16)&31u,true);
    const auto adst=OriginalBlend((packed>>24)&31u,true);
    const auto aop=OriginalBlendOp((packed>>21)&7u);
    if(!src || !dst || !op || !asrc || !adst || !aop ||
       target.SrcBlend!=*src || target.DestBlend!=*dst ||
       target.BlendOp!=*op || target.SrcBlendAlpha!=*asrc ||
       target.DestBlendAlpha!=*adst || target.BlendOpAlpha!=*aop)
      return false;
  }
  return true;
}
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
