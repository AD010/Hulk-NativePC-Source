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
Diligent::StencilOpDesc OriginalStencilFace(uint32_t encoded) noexcept {
  using namespace Diligent;
  constexpr std::array<STENCIL_OP,8> ops{
      STENCIL_OP_KEEP,STENCIL_OP_ZERO,STENCIL_OP_REPLACE,
      STENCIL_OP_INCR_SAT,STENCIL_OP_DECR_SAT,STENCIL_OP_INVERT,
      STENCIL_OP_INCR_WRAP,STENCIL_OP_DECR_WRAP};
  return {ops[(encoded>>3u)&7u],ops[(encoded>>9u)&7u],
          ops[(encoded>>6u)&7u],*OriginalCompare(encoded&7u)};
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
std::optional<Diligent::GraphicsPipelineDesc> DeriveOriginalXdkGraphicsPipelineState(
    const xdk::OriginalXdkFixedGraphicsState& source,
    const Diligent::GraphicsPipelineDesc& source_proven_attachments) noexcept {
  using namespace Diligent;
  if(!source.original_guest_device ||
     source_proven_attachments.NumRenderTargets!=1 ||
     source_proven_attachments.RTVFormats[0]==TEX_FORMAT_UNKNOWN ||
     source_proven_attachments.PrimitiveTopology!=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    return {};
  const auto depth=source.ReadOriginalXenosRegister(0x2200u);
  const auto blend=source.ReadOriginalXenosRegister(0x2201u);
  const auto color=source.ReadOriginalXenosRegister(0x2202u);
  const auto raster=source.ReadOriginalXenosRegister(0x2205u);
  const auto color_mask=source.ReadOriginalXenosRegister(0x2104u);
  const auto surface=source.ReadOriginalXenosRegister(0x2000u);
  const auto original_color_rt=source.ReadOriginalXenosRegister(0x2001u);
  const auto original_depth_rt=source.ReadOriginalXenosRegister(0x2002u);
  const auto clip=source.ReadOriginalXenosRegister(0x2204u);
  if(!depth || !blend || !color || !raster || !color_mask ||
     !surface || !original_color_rt || !original_depth_rt || !clip)
    return {};
  // The actual current Xbox GPU RT format is not supplied by a shader
  // effect name or an archived texture. It is the ACTIVE XDK RB_COLOR_INFO
  // register at the identical original Draw. Do not render a FP16 guest RT
  // through an RGBA8 native PSO or vice versa. Xenos gamma (format 1),
  // integer, FP10 and special 16-bit formats need distinct conversions.
  const uint32_t source_color_format=(*original_color_rt>>16u)&15u;
  const auto expected_color=source_color_format==0u ?
      TEX_FORMAT_RGBA8_UNORM : source_color_format==7u ?
      TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_UNKNOWN;
  if(expected_color==TEX_FORMAT_UNKNOWN ||
     source_proven_attachments.RTVFormats[0]!=expected_color)
    return {};
  // D24FS8 uses an original Xbox 20e4 depth encoding, not a Vulkan
  // D32 float attachment. The sampled original bound guest depth object
  // is independently required before the pass can be declared verified.
  if(source_proven_attachments.DSVFormat!=TEX_FORMAT_UNKNOWN &&
     (source_proven_attachments.DSVFormat!=TEX_FORMAT_D24_UNORM_S8_UINT ||
      ((*original_depth_rt>>16u)&1u)!=0u))
    return {};
  // Only directly equivalent Xenos states are emitted as a native Vulkan
  // PSO. Alpha test/coverage require faithful source pixel-shader lowering,
  // distinct per-face stencil masks require separate Vulkan state, and
  // polygon offset requires exact Xenos bias conversion.
  const uint32_t polygon_mode=(*raster>>3)&3u;
  const uint32_t polygon_front=(*raster>>5)&7u;
  const uint32_t polygon_back=(*raster>>8)&7u;
  if((*color&0x18u) || (*clip&0x3fu) ||
     ((*raster&3u)==3u) ||
     polygon_mode==3u ||
     (polygon_mode==1u && (polygon_front!=2u || polygon_back!=2u)) ||
     (*raster&(7u<<11)) || (*raster&(1u<<20)))
    return {};
  const uint32_t samples=(*surface>>16u)&3u;
  if(samples>2u)return {};
  const uint32_t src_raw=*blend&31u,dst_raw=(*blend>>8u)&31u;
  const uint32_t alpha_src_raw=(*blend>>16u)&31u;
  const uint32_t alpha_dst_raw=(*blend>>24u)&31u;
  const auto src=OriginalBlend(src_raw,false);
  const auto dst=OriginalBlend(dst_raw,false);
  const auto op=OriginalBlendOp((*blend>>5u)&7u);
  const auto asrc=OriginalBlend(alpha_src_raw,true);
  const auto adst=OriginalBlend(alpha_dst_raw,true);
  const auto aop=OriginalBlendOp((*blend>>21u)&7u);
  const auto compare=OriginalCompare((*depth>>4u)&7u);
  if(!src || !dst || !op || !asrc || !adst || !aop || !compare)
    return {};
  auto result=source_proven_attachments;
  result.SmplDesc.Count=1u<<samples;
  // Attachments are source-owned by the caller. A depth-enabled original
  // game Draw cannot be represented by an absent Vulkan depth attachment.
  const bool z_enabled=(*depth&2u)!=0u;
  if(z_enabled && result.DSVFormat==TEX_FORMAT_UNKNOWN)return {};
  result.DepthStencilDesc.DepthEnable=z_enabled;
  result.DepthStencilDesc.DepthWriteEnable=z_enabled && ((*depth&4u)!=0u);
  result.DepthStencilDesc.DepthFunc=*compare;
  auto& stencil=result.DepthStencilDesc;
  stencil.StencilEnable=(*depth&1u)!=0u;
  if(stencil.StencilEnable) {
    if(result.DSVFormat!=TEX_FORMAT_D24_UNORM_S8_UINT)return {};
    const auto front=source.ReadOriginalXenosRegister(0x210du);
    const auto back=source.ReadOriginalXenosRegister(0x210cu);
    if(!front || !back)return {};
    const bool two_sided=(*depth&0x80u)!=0;
    // Diligent exposes a single dynamic reference and shared masks.
    // Preserve independent front/back ops; don't merge distinct masks.
    if(two_sided && ((*front^*back)&0x00ffffffu))return {};
    stencil.StencilReadMask=static_cast<Uint8>(*front>>8u);
    stencil.StencilWriteMask=static_cast<Uint8>(*front>>16u);
    stencil.FrontFace=OriginalStencilFace(*depth>>8u);
    stencil.BackFace=two_sided ? OriginalStencilFace(*depth>>20u) : stencil.FrontFace;
  }
  result.RasterizerDesc.FillMode=FILL_MODE_SOLID;
  result.RasterizerDesc.CullMode=(*raster&1u) ? CULL_MODE_FRONT :
      (*raster&2u) ? CULL_MODE_BACK : CULL_MODE_NONE;
  result.RasterizerDesc.FrontCounterClockwise=(*raster&4u)==0u;
  result.BlendDesc.AlphaToCoverageEnable=false;
  result.BlendDesc.IndependentBlendEnable=false;
  auto& target=result.BlendDesc.RenderTargets[0];
  const bool opaque=src_raw==1u && dst_raw==0u &&
      ((*blend>>5u)&7u)==0u && alpha_src_raw==1u &&
      alpha_dst_raw==0u && ((*blend>>21u)&7u)==0u;
  target.BlendEnable=!opaque;
  target.LogicOperationEnable=false;
  target.RenderTargetWriteMask=static_cast<COLOR_MASK>(*color_mask&15u);
  target.SrcBlend=*src;target.DestBlend=*dst;target.BlendOp=*op;
  target.SrcBlendAlpha=*asrc;target.DestBlendAlpha=*adst;
  target.BlendOpAlpha=*aop;
  return result;
}
bool MatchesOriginalXdkGraphicsState(
    const VerifiedOriginalGraphicsPass& original,
    const xdk::OriginalXdkFixedGraphicsState& source) noexcept {
  if(!original.material_pass_verified ||
     !original.original_render_state_verified ||
     !source.original_guest_device)return false;
  const auto derived=DeriveOriginalXdkGraphicsPipelineState(
      source,original.exact_render_state);
  if(!derived)return false;
  const auto& expected=original.exact_render_state;
  const auto& state=*derived;
  if(expected.SmplDesc.Count!=state.SmplDesc.Count ||
     expected.DepthStencilDesc.DepthEnable!=state.DepthStencilDesc.DepthEnable ||
     expected.DepthStencilDesc.DepthWriteEnable!=
        state.DepthStencilDesc.DepthWriteEnable ||
     expected.DepthStencilDesc.StencilEnable!=state.DepthStencilDesc.StencilEnable ||
     (state.DepthStencilDesc.StencilEnable &&
       (expected.DepthStencilDesc.StencilReadMask!=state.DepthStencilDesc.StencilReadMask ||
        expected.DepthStencilDesc.StencilWriteMask!=state.DepthStencilDesc.StencilWriteMask ||
        !(expected.DepthStencilDesc.FrontFace==state.DepthStencilDesc.FrontFace) ||
        !(expected.DepthStencilDesc.BackFace==state.DepthStencilDesc.BackFace))) ||
     (expected.DepthStencilDesc.DepthEnable &&
       expected.DepthStencilDesc.DepthFunc!=state.DepthStencilDesc.DepthFunc) ||
     (original.original_depth_unbound &&
       (state.DepthStencilDesc.DepthEnable || state.DepthStencilDesc.StencilEnable)) ||
     expected.RasterizerDesc.CullMode!=state.RasterizerDesc.CullMode ||
     expected.RasterizerDesc.FrontCounterClockwise!=
        state.RasterizerDesc.FrontCounterClockwise ||
     expected.RasterizerDesc.FillMode!=state.RasterizerDesc.FillMode ||
     expected.BlendDesc.AlphaToCoverageEnable)
    return false;
  const auto& provided=expected.BlendDesc.RenderTargets[0];
  const auto& checked=state.BlendDesc.RenderTargets[0];
  if(provided.BlendEnable!=checked.BlendEnable ||
     provided.RenderTargetWriteMask!=checked.RenderTargetWriteMask ||
     provided.LogicOperationEnable)return false;
  if(!checked.BlendEnable)return true;
  return provided.SrcBlend==checked.SrcBlend &&
         provided.DestBlend==checked.DestBlend &&
         provided.BlendOp==checked.BlendOp &&
         provided.SrcBlendAlpha==checked.SrcBlendAlpha &&
         provided.DestBlendAlpha==checked.DestBlendAlpha &&
         provided.BlendOpAlpha==checked.BlendOpAlpha;
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
