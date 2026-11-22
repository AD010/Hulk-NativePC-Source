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
    const aot::EffectCatalog& original_effects,
    const VerifiedOriginalGraphicsPass& verified);
} // namespace hulk::eor::gpu
