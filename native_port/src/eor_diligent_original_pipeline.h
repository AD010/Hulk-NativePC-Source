#pragma once
// Direct host Vulkan PSO for a VERIFIED EoR material effect. Xenos PM4, EDRAM,
// guest register snapshots and speculative shader states are not consulted.
#include "eor_diligent_spirv_shaders.h"
#include "eor_native_shader_constants.h"
#include "eor_effect_catalog.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"
#include "RefCntAutoPtr.hpp"
#include <cstdint>
#include <optional>
#include <vector>
namespace Diligent {
struct IRenderDevice;
struct IPipelineState;
struct IShaderResourceBinding;
}
namespace hulk::eor::gpu {
// The native presenter draws a WORLD viewport over every host output but
// centers ORIGINAL 16:9 console HUD to preserve readable, unstretched UI.
// Only live-source-verified owners may select a nondefault viewport. Offscreen
// shadow/reflection/video targets are not equivalent to the main screen.
enum class VerifiedOriginalViewSpace : uint8_t {
  Unknown=0, MainPerspective=1, OriginalAspectHud=2,
  FullOutputPost=3, Offscreen=4
};
// This is a NATIVE shader-constant fix, not a mutation of game guest memory.
// Source evidence must identify the original perspective main 3D view,
// its c# ViewProj range, matrix multiplication order and guest aspect.
struct VerifiedOriginalMainView {
  camera::SourceCameraProof source_proof;
  uint32_t original_view_projection_first_register=UINT32_MAX;
  double original_guest_aspect=0.0;
  // The direct256 scalar path proves its four original register loads
  // and validates the actual perspective matrix each Draw.
  bool live_scalar_matrix_proof=false;
};
struct VerifiedOriginalGraphicsPass {
  uint32_t original_effect_hash=0;
  uint32_t original_pass_index=UINT32_MAX;
  aot::Digest original_vertex_shader_sha256{};
  aot::Digest original_pixel_shader_sha256{};
  Diligent::GraphicsPipelineDesc exact_render_state{};
  std::vector<Diligent::LayoutElement> exact_host_input_layout;
  // These maps MUST be sourced from the exact analyzed Xenos VS/PS ucode
  // and its translated SPIR-V XeFloatConstants layout, not inferred from
  // the camera matrix names or the ShaderEffects pass ID.
  std::optional<aot::VerifiedOriginalFloatLayout> original_vs_float_layout;
  std::optional<aot::VerifiedOriginalFloatLayout> original_ps_float_layout;
  // Only this verified main perspective pass gets 16:10 clip-space Y
  // correction; shadows, reflections, videos and HUD remain untouched.
  std::optional<VerifiedOriginalMainView> source_main_view;
  VerifiedOriginalViewSpace original_view_space=VerifiedOriginalViewSpace::Unknown;
  bool original_view_space_verified=false;
  // Requires the original HUD's NDC clip coordinates to be observed;
  // guest-space 720p vertex pixels need separate vertex conversion.
  bool original_hud_clip_coordinates_verified=false;
  bool material_pass_verified=false;
  bool original_render_state_verified=false;
  bool original_host_vertex_layout_verified=false;
  // Source-proof that this particular ORIGINAL EFontShader pass consumes
  // independently decoded transient float4 font attributes, NOT static
  // MODELS.ARC XYZ meshes. Verified against the live original declaration,
  // shader input layout, pass state and atlas ownership by the installer.
  bool original_dynamic_font_vertex_layout_verified=false;
  // The original pass has no depth/stencil attachment. Unknown DSV format
  // alone is insufficient, as it can also indicate missing format evidence.
  bool original_depth_unbound=false;
  // The scalar AOT PS contains the original alpha-kill lowering. The
  // caller must bind the current reference and enable flag per Draw.
  bool original_scalar_alpha_test_verified=false;
  uint16_t original_xbox_cube_gamma_mask=0;
};
struct OriginalVulkanPipeline {
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> bindings;
};
// The original 6534 device has live per-Draw Xenos raster/depth/blend
// register shadow. Verify this exact source state against a material's
// already source-authenticated Vulkan PSO before accepting its Draw. Source
// PSO ownership and vertex/texture identities remain independent gates.
// Derive the live source Xenos color-mask/blend, depth, cull and MSAA
// directly into the existing Diligent Vulkan graphics descriptor. The input
// descriptor must already carry independently verified attachment formats,
// topology and input layout. The original XDK state can never prove those.
std::optional<Diligent::GraphicsPipelineDesc> DeriveOriginalXdkGraphicsPipelineState(
    const xdk::OriginalXdkFixedGraphicsState& source,
    const Diligent::GraphicsPipelineDesc& source_proven_attachments) noexcept;
// The compiled scalar PS uses clip(alpha-reference), equivalent to
// GREATER_EQUAL (and ALWAYS with the flag disabled). Other comparisons
// require additional original shader lowering, not threshold guesses.
struct OriginalScalarAlphaState {float threshold=0;uint32_t flags=0;};
inline std::optional<OriginalScalarAlphaState> ResolveOriginalScalarAlpha(
    const xdk::OriginalXdkFixedGraphicsState& state) noexcept {
  const auto control=state.ReadOriginalXenosRegister(0x2202u);
  if(!control || (*control&16u))return {};
  if(!(*control&8u) || (*control&7u)==7u)return OriginalScalarAlphaState{};
  if((*control&7u)!=6u)return {};
  const auto reference=state.ReadOriginalXenosRegister(0x210eu);
  if(!reference)return {};
  const auto threshold=std::bit_cast<float>(*reference);
  if(!std::isfinite(threshold))return {};
  return OriginalScalarAlphaState{threshold,2u};
}
bool MatchesOriginalXdkGraphicsState(
    const VerifiedOriginalGraphicsPass& original,
    const xdk::OriginalXdkFixedGraphicsState& source) noexcept;
// Creates a real SPIR-V-backed Vulkan PSO and SRB, only after the calling
// engine bridge supplies a source-proven pass, color/depth formats, MSAA,
// and a vertex layout. No guessed blend/depth/culling/sampler state.
OriginalVulkanPipeline CreateOriginalVulkanPipeline(
    Diligent::IRenderDevice* device,
    DiligentAotShaderModules& native_shaders,
    const aot::ShaderBank& bank,
    const aot::EffectCatalog& original_effects,
    const VerifiedOriginalGraphicsPass& verified);
} // namespace hulk::eor::gpu
