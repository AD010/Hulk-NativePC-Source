#include "eor_diligent_aot_backend.h"
#include "eor_spirv_texture_bindings.h"
#include "native_resolution.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Buffer.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"
#include "TextureView.h"
#include "RefCntAutoPtr.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <tuple>
#include <stdexcept>
#include <utility>

namespace hulk::eor::gpu {
namespace {
bool Same(native::Size a,native::Size b) {
  return a.width==b.width && a.height==b.height;
}
bool Same(native::Rect a,native::Rect b) {
  return a.left==b.left && a.top==b.top &&
         a.width==b.width && a.height==b.height;
}
bool Same(const VerifiedEorSampler& a,const VerifiedEorSampler& b) {
  return a.original_model_hash==b.original_model_hash &&
         a.original_surface_ordinal==b.original_surface_ordinal &&
         a.original_effect_hash==b.original_effect_hash &&
         a.original_pass_index==b.original_pass_index &&
         a.original_texture_hash==b.original_texture_hash &&
         a.original_fetch_register==b.original_fetch_register &&
         a.stage==b.stage &&
         a.exact_shader_variable_name==b.exact_shader_variable_name &&
         a.original_material_ownership_verified==b.original_material_ownership_verified &&
         a.original_shader_fetch_slot_verified==b.original_shader_fetch_slot_verified &&
         a.original_texture_format_and_mips_verified==b.original_texture_format_and_mips_verified;
}
// The entire original shader image/SRV set must be a one-to-one mapping.
// Perform this exact check in BOTH frame preflight and live Draw binding;
// duplicate samplers must fail before the first native Vulkan clear/Draw.
bool HasExactSourceSamplerBijection(
    std::span<const VerifiedEorSampler> expected,
    std::span<const OriginalDiligentSampler> resident) {
  if(expected.size()!=resident.size())return false;
  std::set<size_t> matched;
  for(const auto& slot:expected) {
    size_t unique=resident.size();
    for(size_t i=0;i<resident.size();++i)
      if(Same(slot,resident[i].identity)) {
        if(unique!=resident.size())return false;
        unique=i;
      }
    if(unique==resident.size() || !matched.insert(unique).second)
      return false;
  }
  return matched.size()==resident.size();
}
}
std::vector<NativeEorPassResources> AcquireVerifiedNativeMaterialPasses(
    std::span<const VerifiedNativeMaterialPass> verified,
    DiligentOriginalTextureResidency& residency) {
  if(verified.empty())
    throw std::invalid_argument("No original source-proven EoR material passes");
  std::set<std::tuple<uint32_t,uint32_t,uint32_t,uint32_t>> keys;
  // Validate every material/pass owner BEFORE the first original GPU image
  // allocation; never replace a missing Texture with a synthetic SRV.
  for(const auto& p:verified) {
    const auto& pass=p.original_pipeline;
    if(!p.original_model_hash || p.original_surface_ordinal==UINT32_MAX ||
       !pass.original_effect_hash || pass.original_pass_index==UINT32_MAX ||
       !pass.material_pass_verified || !pass.original_render_state_verified ||
       !pass.original_host_vertex_layout_verified ||
       !keys.emplace(p.original_model_hash,p.original_surface_ordinal,
                     pass.original_effect_hash,pass.original_pass_index).second)
      throw std::invalid_argument("Ambiguous or unverified original EoR material pass");
    std::vector<VerifiedEorSampler> identifiers;
    identifiers.reserve(p.original_images.size());
    for(const auto& image:p.original_images) {
      const auto& owner=image.original_binding;
      if(owner.original_model_hash!=p.original_model_hash ||
         owner.original_surface_ordinal!=p.original_surface_ordinal ||
         owner.original_effect_hash!=pass.original_effect_hash ||
         owner.original_pass_index!=pass.original_pass_index ||
         image.source_proven_color_space==OriginalTextureColorSpace::Unknown)
        throw std::invalid_argument("Original EoR texture has wrong surface/PSO owner");
      identifiers.push_back(owner);
    }
    ValidateOriginalEorSamplers(identifiers);
  }
  std::vector<NativeEorPassResources> ready;
  ready.reserve(verified.size());
  for(const auto& p:verified) {
    NativeEorPassResources gpu;
    gpu.original_model_hash=p.original_model_hash;
    gpu.original_surface_ordinal=p.original_surface_ordinal;
    gpu.pipeline=p.original_pipeline;
    // This loads original pinned-USA TexStreams mips directly into resident
    // Diligent/Vulkan SRVs. Each descriptor retains ownership across cache
    // eviction and through the caller's GPU completion fence.
    gpu.original_textures=residency.BindMaterial(p.original_images);
    ready.push_back(std::move(gpu));
  }
  return ready;
}
std::vector<NativeEorDrawBuffers> PositionOnlyDrawBuffersFromOriginalModels(
    std::span<const model::UploadedOriginalModel* const> residents) {
  if(residents.empty())
    throw std::invalid_argument("No original Xbox EoR resident models");
  std::set<uint32_t> unique_models;
  std::set<uint64_t> unique_positions;
  std::vector<NativeEorDrawBuffers> result;
  for(const auto* resident_model:residents) {
    if(!resident_model || !resident_model->original_resource_hash ||
       resident_model->original_identity.platform!=model::Platform::Xbox360 ||
       !unique_models.insert(resident_model->original_resource_hash).second)
      throw std::invalid_argument("Ambiguous original EoR model residency");
    for(const auto& mesh:resident_model->meshes.meshes()) {
      if(!mesh.position_buffer || !mesh.index_buffer ||
         !mesh.original_vertex_stream_buffer ||
         mesh.original_vertex_stride<12 || mesh.vertex_count<3 ||
         !mesh.index_count || mesh.index_count%3 ||
         !unique_positions.insert(mesh.position_buffer).second)
        throw std::invalid_argument("Incomplete original EoR GPU mesh allocation");
      NativeEorDrawBuffers draw;
      draw.position_handle=mesh.position_buffer;
      draw.index_handle=mesh.index_buffer;
      draw.vertex_count=mesh.vertex_count;
      draw.index_count=mesh.index_count;
      draw.host_vertex_slots={mesh.position_buffer};
      for(const uint64_t slot:mesh.decoded_attribute_buffers) {
        if(!slot)throw std::invalid_argument("Original mesh decoded attribute not resident");
        draw.host_vertex_slots.push_back(slot);
      }
      draw.original_model_hash=resident_model->original_resource_hash;
      draw.original_mesh_header_offset=mesh.original_header_offset;
      result.push_back(std::move(draw));
    }
  }
  if(result.empty())
    throw std::invalid_argument("No indexed Xbox source geometry resident");
  return result;
}
struct DiligentEorDirectBackend::State {
  using MaterialKey=std::tuple<uint32_t,uint32_t,uint32_t,uint32_t>;
  State(Diligent::IRenderDevice* d,Diligent::IDeviceContext* c,
        DiligentSceneTargets& sc,DiligentMeshUploadBackend& ms,
        DiligentAotShaderModules& shaders,const aot::ShaderBank& bank,
        const aot::EffectCatalog& effects,native::FrameGeometry f,
        VerifiedSceneFormat formats,std::vector<NativeEorPassResources> passes,
        std::vector<NativeEorDrawBuffers> buffers,BindConstants bind,
        CompositeOutput present)
      :device(d),context(c),scene(sc),meshes(ms),shaders(shaders),
       bank(bank),effects(effects),frame(f),format(formats),
       passes(std::move(passes)),bind_constants(std::move(bind)),
       composite(std::move(present)) {
    // Pre-index exact source model/surface/effect/pass ownership ONCE at
    // asset load, not O(draws * all-materials) on every game frame.
    // Pointers remain stable: the resident pass vector is never reallocated.
    for(const auto& original:passes) {
      const MaterialKey key{original.original_model_hash,
          original.original_surface_ordinal,
          original.pipeline.original_effect_hash,
          original.pipeline.original_pass_index};
      if(!material_lookup.emplace(key,&original).second)
        throw std::invalid_argument("Duplicate native EoR surface/material pass");
    }
    for(auto& entry:buffers)
      if(!draw_buffers.emplace(entry.position_handle,std::move(entry)).second)
        throw std::invalid_argument("Duplicate original EoR Draw buffer provenance");
  }
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
  DiligentSceneTargets& scene;
  DiligentMeshUploadBackend& meshes;
  DiligentAotShaderModules& shaders;
  const aot::ShaderBank& bank;
  const aot::EffectCatalog& effects;
  native::FrameGeometry frame;
  VerifiedSceneFormat format;
  std::vector<NativeEorPassResources> passes;
  std::map<uint64_t,NativeEorDrawBuffers> draw_buffers;
  BindConstants bind_constants;
  CompositeOutput composite;
  const NativeEorPassResources* selected=nullptr;
  uint32_t requested_model=0, requested_surface=UINT32_MAX;
  bool surface_ready=false;
  aot::ReflectedShader vertex{},pixel{};
  OriginalVulkanPipeline active{};
  // Shader MODULES are cached by digest, but host PSOs are keyed by the
  // EXACT original model/surface/effect/pass: two materials may share a
  // ShaderEffect pair yet use different source-proven blend/depth/cull or
  // vertex input state. Sharing a PSO keyed by technique alone was unsafe.
  std::map<MaterialKey,const NativeEorPassResources*> material_lookup;
  std::map<MaterialKey,OriginalVulkanPipeline> pipeline_cache;
  // Each original pass owns a unique mutable Vulkan descriptor binding.
  std::map<MaterialKey,Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>>
      material_bindings;
  // Per actual guest frame/ordinal, with no shader remapping or inter-Draw
  // state inheritance. Only source-proven translator-used floats are packed.
  using DrawKey=std::pair<uint64_t,uint32_t>;
  struct PackedConstants {
    std::vector<float> vertex,pixel;
  };
  std::map<DrawKey,PackedConstants> packed_constants;
  bool frame_preflight_ready=false;
  bool scene_open=false,constants_ready=false,program_ready=false;
  bool textures_ready=false,finished=false;
  bool source_timeline_active=false;
  bool source_resolve_recorded=false;
  bool source_present_recorded=false;
  // One source frame can authorize at most one physical host Present attempt.
  bool composite_attempted=false;
};
DiligentEorDirectBackend::DiligentEorDirectBackend(
    Diligent::IRenderDevice* device,Diligent::IDeviceContext* context,
    DiligentSceneTargets& scene,DiligentMeshUploadBackend& meshes,
    DiligentAotShaderModules& shaders,const aot::ShaderBank& bank,
    const aot::EffectCatalog& effects,native::FrameGeometry frame,
    VerifiedSceneFormat formats,std::vector<NativeEorPassResources> passes,
    std::vector<NativeEorDrawBuffers> buffers,BindConstants constants,
    CompositeOutput composite)
    :state_(device && context ? std::make_unique<State>(
        device,context,scene,meshes,shaders,bank,effects,frame,formats,
        std::move(passes),std::move(buffers),std::move(constants),
        std::move(composite)) : nullptr) {
  if(!state_ || device->GetDeviceInfo().Type != Diligent::RENDER_DEVICE_TYPE_VULKAN ||
     !frame.direct_engine_path_ready || !state_->bind_constants ||
     !state_->composite || !bank.size() ||
     effects.ShaderCount()!=bank.expected() || state_->passes.empty() ||
     state_->draw_buffers.empty())
    throw std::invalid_argument("Original EoR engine Draw/constant/output Vulkan ABI not verified");
  if(!formats.original_color_format_verified ||
     !formats.original_depth_format_verified)
    throw std::invalid_argument("Native EoR scene color and depth format not source-proven");
  const Diligent::TEXTURE_FORMAT color =
      formats.color==NativeColorFormat::Rgba8 ?
          Diligent::TEX_FORMAT_RGBA8_UNORM :
      formats.color==NativeColorFormat::Rgba16Float ?
          Diligent::TEX_FORMAT_RGBA16_FLOAT : Diligent::TEX_FORMAT_UNKNOWN;
  const Diligent::TEXTURE_FORMAT depth =
      formats.depth==NativeDepthFormat::D24S8 ?
          Diligent::TEX_FORMAT_D24_UNORM_S8_UINT :
      formats.depth==NativeDepthFormat::D32Float ?
          Diligent::TEX_FORMAT_D32_FLOAT : Diligent::TEX_FORMAT_UNKNOWN;
  if(color==Diligent::TEX_FORMAT_UNKNOWN || depth==Diligent::TEX_FORMAT_UNKNOWN)
    throw std::invalid_argument("Unknown EoR native scene format");
  for(size_t i=0;i<state_->passes.size();++i) {
    const auto& owner=state_->passes[i];
    const auto& pass=owner.pipeline;
    if(frame.widescreen_camera) {
      // A verified original main perspective may expand into the full
      // native viewport. The original screen-space HUD must remain at its
      // own safe 16:9 aspect, and unverified offscreen/tiling passes may NOT
      // be stretched to the host window just because the output is ultrawide.
      if(!pass.original_view_space_verified ||
         pass.original_view_space==VerifiedOriginalViewSpace::Unknown ||
         pass.original_view_space==VerifiedOriginalViewSpace::Offscreen ||
         (pass.original_view_space==VerifiedOriginalViewSpace::MainPerspective &&
          !pass.source_main_view) ||
         (pass.original_view_space==VerifiedOriginalViewSpace::OriginalAspectHud &&
          !pass.original_hud_clip_coordinates_verified))
        throw std::invalid_argument(
            "Native aspect requires source-verified 3D/HUD/pass viewport ownership");
    }
    if(!owner.original_model_hash || owner.original_surface_ordinal==UINT32_MAX)
      throw std::invalid_argument("Original EoR material surface provenance missing");
    for(const auto& texture:owner.original_textures)
      if(texture.identity.original_model_hash!=owner.original_model_hash ||
         texture.identity.original_surface_ordinal!=owner.original_surface_ordinal ||
         texture.identity.original_effect_hash!=pass.original_effect_hash ||
         texture.identity.original_pass_index!=pass.original_pass_index)
        throw std::invalid_argument("Original EoR texture belongs to different material surface");
    // Verify the SOURCE shader's actual OpTypeImage dimensionality. An
    // original GPU mip chain uploaded as Texture2DArray cannot be sampled
    // from a Texture2D binding (or vice versa); one wrong view corrupts
    // the whole material even though the source bytes are correct.
    for(const auto stage:{aot::Stage::Vertex,aot::Stage::Pixel}) {
      const auto* binary=bank.Find(
          stage==aot::Stage::Vertex ? pass.original_vertex_shader_sha256 :
                                      pass.original_pixel_shader_sha256,stage);
      if(!binary)
        throw std::invalid_argument("Original EoR material shader absent from native AOT bank");
      const auto reflected=aot::InspectSpirvImageResources(binary->spirv);
      for(const auto& image:reflected) {
        if(image.spirv_dim!=1 || image.image_multisampled)
          throw std::invalid_argument("Original EoR material requires unsupported non-2D or multisampled shader image");
        const auto view=image.image_arrayed ?
            OriginalTextureViewKind::NativeX2DArray :
            OriginalTextureViewKind::Native2D;
        unsigned matches=0;
        for(const auto& texture:owner.original_textures)
          if(texture.identity.stage==
                (stage==aot::Stage::Vertex ? OriginalSamplerStage::Vertex :
                                             OriginalSamplerStage::Pixel) &&
             texture.identity.exact_shader_variable_name==image.exact_variable_name) {
            if(texture.required_view!=view || !texture.uploaded ||
               texture.uploaded->view_kind!=view)
              throw std::invalid_argument("EoR original texture view disagrees with actual SPIR-V image type");
            ++matches;
          }
        if(matches!=1)
          throw std::invalid_argument("Original EoR shader image has no exact material owner/SRV");
      }
      for(const auto& texture:owner.original_textures) {
        if(texture.identity.stage !=
            (stage==aot::Stage::Vertex ? OriginalSamplerStage::Vertex :
                                         OriginalSamplerStage::Pixel))continue;
        bool present=false;
        for(const auto& image:reflected)
          if(texture.identity.exact_shader_variable_name==
             image.exact_variable_name)present=true;
        if(!present)
          throw std::invalid_argument("Native material contains an image not declared by original shader");
      }
    }
    const auto& gpu=pass.exact_render_state;
    // The owned host scene has exactly one color RT and one depth RT.
    // Never bind a PSO expecting mismatched sample counts or formats.
    if(gpu.NumRenderTargets!=1 || gpu.RTVFormats[0]!=color ||
       gpu.DSVFormat!=depth || gpu.SmplDesc.Count!=frame.samples)
      throw std::invalid_argument("Native Vulkan PSO format/sample contract disagrees with scene");
    uint32_t used_slots=0;
    for(const auto& v:pass.exact_host_input_layout) {
      if(v.BufferSlot>=16 || (used_slots & (1u << v.BufferSlot)) ||
         v.ValueType!=Diligent::VT_FLOAT32 ||
         v.Frequency!=Diligent::INPUT_ELEMENT_FREQUENCY_PER_VERTEX ||
         (v.RelativeOffset!=0 &&
          v.RelativeOffset!=Diligent::LAYOUT_ELEMENT_AUTO_OFFSET) ||
         v.NumComponents==0 || v.NumComponents>4)
        throw std::invalid_argument("Unverified independent EoR float vertex slot layout");
      const uint32_t stride=v.BufferSlot==0?12:16;
      if(v.Stride!=stride && v.Stride!=Diligent::LAYOUT_ELEMENT_AUTO_STRIDE)
        throw std::invalid_argument("EoR vertex strides do not match decoded host data");
      if(v.BufferSlot==0 && v.NumComponents!=3)
        throw std::invalid_argument("EoR host position buffer requires float3 XYZ");
      used_slots |= 1u << v.BufferSlot;
    }
    const uint32_t count=static_cast<uint32_t>(pass.exact_host_input_layout.size());
    if(count==0 || count>16 || used_slots!=((1u << count)-1u))
      throw std::invalid_argument("EoR shader vertex inputs have gaps/duplicates");
    if(!effects.HasPair(pass.original_effect_hash,pass.original_pass_index,
                        pass.original_vertex_shader_sha256,
                        pass.original_pixel_shader_sha256))
      throw std::invalid_argument("Native EoR shader/pass provenance mismatched");
    // State::material_lookup already rejects duplicate four-part owners
    // in O(log N) per pass, before any Vulkan pipeline is allocated.
  }
  for(const auto& [position,resident]:state_->draw_buffers) {
    const auto& slots=resident.host_vertex_slots;
    if(!position || resident.position_handle!=position ||
       !resident.original_model_hash ||
       resident.original_mesh_header_offset==SIZE_MAX ||
       !resident.index_handle || resident.vertex_count<3 ||
       !resident.index_count || (resident.index_count%3)!=0 ||
       slots.empty() || slots.size()>16 ||
       slots[0]!=position || state_->meshes.KindOf(position)!=0 ||
       state_->meshes.KindOf(resident.index_handle)!=2)
      throw std::invalid_argument("Missing EoR host mesh vertex/index provenance");
    // Full-buffer extrema are not valid per-Draw bounds: an indexed
    // subrange can legitimately use a negative signed baseVertex.
    // Validate actual windows below in PreflightOriginalScene/DrawIndexed.
    const auto* vb=state_->meshes.FindBuffer(position);
    const auto* ib=state_->meshes.FindBuffer(resident.index_handle);
    if(!vb || !ib ||
       uint64_t(resident.vertex_count)>vb->GetDesc().Size/12u ||
       uint64_t(resident.index_count)>ib->GetDesc().Size/sizeof(uint32_t))
      throw std::invalid_argument("EoR mesh residency exceeds GPU buffer allocation");
    for(size_t i=1;i<slots.size();++i) {
      if(!slots[i] || state_->meshes.KindOf(slots[i])!=3)
        throw std::invalid_argument("Raw Xbox BE vertex bytes cannot bind to host Vulkan");
      const auto* attributes=state_->meshes.FindBuffer(slots[i]);
      if(!attributes ||
         uint64_t(resident.vertex_count)>attributes->GetDesc().Size/(4u*sizeof(float)))
        throw std::invalid_argument("EoR decoded float4 vertex attributes are truncated");
    }
  }
  // Load-stage AOT pipeline preparation as used by Sonic/Blue Dragon:
  // shaders for the resident models are compiled BEFORE the first game
  // frame, never in the middle of an indexed Draw. Keep a bounded startup
  // batch; the full scene preflight compiles any remaining source passes.
  (void)PrewarmResidentOriginalPipelines(64);
}
DiligentEorDirectBackend::~DiligentEorDirectBackend() noexcept {AbortScene();}
bool DiligentEorDirectBackend::CanReplayOriginalXdkCommand(
    const xdk::RawCommand& cmd) const {
  if(!state_)return false;
  const auto& s=*state_;
  const auto& original=s.format.original_screen;
  if(!original.exact_guest_to_native_targets_verified ||
     !original.guest_device || !original.guest_rt0 ||
     !original.guest_depth || !original.guest_front_buffer ||
     cmd.gpr[0]!=original.guest_device)
    return false;
  switch(cmd.kind) {
    case xdk::CommandKind::SetRenderTarget:
      return cmd.gpr[1]==0 && cmd.gpr[2]==original.guest_rt0;
    case xdk::CommandKind::SetDepthStencilSurface:
      return cmd.gpr[1]==original.guest_depth;
    case xdk::CommandKind::ClearF:
      return cmd.captured_clear &&
          xdk::IsOriginalFullTargetClear(
              *cmd.captured_clear,s.frame.original_guest.width,
              s.frame.original_guest.height) &&
          ((cmd.captured_clear->flags & 0x20u)==0 ||
           s.format.depth==NativeDepthFormat::D24S8);
    case xdk::CommandKind::Resolve:
      return original.final_resolve_to_front_buffer_verified &&
             (cmd.gpr[1]&7u)==0 &&
             (cmd.gpr[1]&0x300u)==0 &&
             cmd.gpr[3]==original.guest_front_buffer;
    case xdk::CommandKind::BeginTiling:
    case xdk::CommandKind::EndTiling:
      return original.original_tiling_has_no_host_aliases_verified;
    case xdk::CommandKind::Present:
      return original.final_resolve_to_front_buffer_verified &&
             cmd.gpr[1]==original.guest_front_buffer;
    default:
      return false;
  }
}
bool DiligentEorDirectBackend::CanReplayOriginalXdkTimeline(
    const xdk::Frame& source) const {
  if(!state_ || !xdk::ValidateOriginalCommandTimeline(source))return false;
  // This native EoR backend owns ONE verified main RT0/depth and ONE final
  // resolve. Other original passes (shadows, MRT, chained post-processing)
  // require distinct native target mappings; rejecting them is safer than
  // pretending a single host RT is equivalent to all guest EDRAM tiles.
  bool rt=false,depth=false,cleared=false,drew=false,resolved=false,tiling=false;
  for(const auto& cmd:source.commands) {
    if(cmd.draw_ordinal!=UINT32_MAX) {
      if(!rt || !depth || !cleared || resolved)return false;
      drew=true;
      continue;
    }
    if(!CanReplayOriginalXdkCommand(cmd))return false;
    switch(cmd.kind) {
      case xdk::CommandKind::SetRenderTarget:
        if(resolved)return false;
        rt=true;break;
      case xdk::CommandKind::SetDepthStencilSurface:
        if(resolved)return false;
        depth=true;break;
      case xdk::CommandKind::ClearF:
        if(resolved || ((cmd.captured_clear->flags&1u) && !rt) ||
           ((cmd.captured_clear->flags&0x30u) && !depth))
          return false;
        cleared=true;break;
      case xdk::CommandKind::BeginTiling:
        if(tiling || resolved || !rt)return false;
        tiling=true;break;
      case xdk::CommandKind::EndTiling:
        if(!tiling || resolved)return false;
        tiling=false;break;
      case xdk::CommandKind::Resolve:
        if(!drew || tiling || resolved)return false;
        resolved=true;break;
      case xdk::CommandKind::Present:
        if(!resolved || !drew || tiling)return false;
        break;
      default:return false;
    }
  }
  return rt && depth && cleared && drew && resolved && !tiling;
}
bool DiligentEorDirectBackend::ReplayOriginalXdkCommand(
    const xdk::RawCommand& cmd) {
  if(!state_ || !CanReplayOriginalXdkCommand(cmd))return false;
  auto& s=*state_;
  if(cmd.kind==xdk::CommandKind::Present) {
    // Validate the source Present BEFORE CompositeOutput touches the
    // physical swapchain. This records authorization, not a GPU Present.
    if(!s.finished || !s.scene_open || !s.source_timeline_active ||
       !s.source_resolve_recorded || s.source_present_recorded)
      return false;
    s.source_present_recorded=true;
    return true;
  }
  if(!s.scene_open || s.finished)return false;
  s.source_timeline_active=true;
  switch(cmd.kind) {
    case xdk::CommandKind::SetRenderTarget:
    case xdk::CommandKind::SetDepthStencilSurface:
    case xdk::CommandKind::BeginTiling:
    case xdk::CommandKind::EndTiling:
      // Host scene is already bound. The exact source target identity and
      // (when tiled) alias-free mapping were proved in frame preflight.
      return true;
    case xdk::CommandKind::ClearF:
      return cmd.captured_clear &&
             s.scene.ClearVerifiedOriginalXdk(
                 *cmd.captured_clear,s.frame.original_guest);
    case xdk::CommandKind::Resolve:
      // The real native color+depth target remains resident through
      // DrawIndexed; physical MSAA resolve is performed by ResolveScene()
      // immediately after the final source Draw, not against guest EDRAM.
      if(s.source_resolve_recorded)return false;
      s.source_resolve_recorded=true;
      return true;
    default:return false;
  }
}

std::size_t DiligentEorDirectBackend::PrewarmResidentOriginalPipelines(
    std::size_t max_new) {
  if(!state_ || !max_new || state_->scene_open ||
     state_->frame_preflight_ready)return 0;
  auto& s=*state_;
  std::set<uint32_t> resident_models;
  for(const auto& [_,mesh]:s.draw_buffers)
    resident_models.insert(mesh.original_model_hash);
  std::size_t warmed=0;
  for(const auto& owner:s.passes) {
    if(warmed>=max_new)break;
    if(!resident_models.contains(owner.original_model_hash))continue;
    const State::MaterialKey key{
        owner.original_model_hash,owner.original_surface_ordinal,
        owner.pipeline.original_effect_hash,
        owner.pipeline.original_pass_index};
    if(s.pipeline_cache.contains(key) &&
       s.material_bindings.contains(key))continue;
    try {
      if(!s.pipeline_cache.contains(key)) {
        // Original material/VS/PS/host-vertex-layout identity is verified
        // during construction; shared shader techniques never override a
        // different surface's fixed-function Vulkan state.
        auto pipeline=CreateOriginalVulkanPipeline(
            s.device.RawPtr(),s.shaders,s.bank,s.effects,owner.pipeline);
        s.pipeline_cache.emplace(key,std::move(pipeline));
      }
      if(!s.material_bindings.contains(key)) {
        const auto it=s.pipeline_cache.find(key);
        if(it==s.pipeline_cache.end() || !it->second.bindings)
          throw std::runtime_error("Verified EoR prewarm lacks original SRB");
        // Reuse the SRB created by the original Vulkan pipeline itself:
        // allocating it AGAIN in Preflight costs driver time and descriptor
        // space. Prebind all source-verified texture/mip SRVs at asset load.
        auto binding=it->second.bindings;
        BindVerifiedOriginalEorTextures(binding.RawPtr(),
                                         owner.original_textures);
        s.material_bindings.emplace(key,std::move(binding));
        ++warmed;
      }
    } catch (...) {
      s.material_bindings.erase(key);
      s.pipeline_cache.erase(key);
      break; // Never let optional prewarm break guest game initialization.
    }
  }
  return warmed;
}
bool DiligentEorDirectBackend::PreflightOriginalScene(
    std::span<const aot::BoundIndexedDraw> draws) {
  auto& s=*state_;
  // A rejected nested preflight must not erase the constant snapshots
  // being consumed by the CURRENT Vulkan scene.
  if(s.scene_open)return false;
  s.frame_preflight_ready=false;
  s.packed_constants.clear();
  if(draws.empty())return false;
  std::map<State::DrawKey,State::PackedConstants> prepared_constants;
  std::set<State::MaterialKey> needed_materials;
  std::set<std::pair<uint64_t,uint32_t>> captured_calls;
  uint64_t captured_frame=0;
  uint32_t next_original_ordinal=0;
  bool saw_captured_draw=false;
  bool saw_anonymous_draw=false;
  // Pass one: validate the ENTIRE frame's native draw and original material
  // ownership. Do not create a PSO before a later invalid draw is rejected.
  for(const auto& item:draws) {
    const auto& d=item.draw;
    if(d.source_frame) {
      // Guest Draws must reach native preflight in the complete captured
      // original order; skipping/reordering one may corrupt resolves/HUD.
      if(saw_anonymous_draw || d.source_ordinal!=next_original_ordinal ||
         (captured_frame && d.source_frame!=captured_frame) ||
         !captured_calls.emplace(d.source_frame,d.source_ordinal).second)
        return false;
      if(next_original_ordinal==UINT32_MAX)return false;
      ++next_original_ordinal;
      saw_captured_draw=true;
      captured_frame=d.source_frame;
    } else {
      if(saw_captured_draw || d.source_ordinal!=UINT32_MAX)return false;
      saw_anonymous_draw=true;
    }
    if(!d.vertex_buffer || !d.index_buffer || !d.vertex_shader ||
       !d.pixel_shader || !d.index_count || !item.original_model_hash ||
       item.original_surface_ordinal==UINT32_MAX || !item.effect_hash ||
       item.pass_index==UINT32_MAX)
      return false;
    const auto owner=s.material_lookup.find(
        {item.original_model_hash,item.original_surface_ordinal,
         item.effect_hash,item.pass_index});
    const NativeEorPassResources* selected=
        owner==s.material_lookup.end()?nullptr:owner->second;
    if(!selected ||
       selected->pipeline.original_vertex_shader_sha256!=item.vertex_source_sha256 ||
       selected->pipeline.original_pixel_shader_sha256!=item.pixel_source_sha256 ||
       selected->original_textures.size()!=item.original_samplers.size())
      return false;
    // The exact per-Draw XDK device shadow is independent of the
    // interleaved command log. Reject unknown/changed RT0 or depth owners
    // BEFORE pipeline compilation, color clears or the first Vulkan Draw.
    if(!d.source_frame || !d.guest_vs_matrices ||
       !MatchesVerifiedOriginalXdkMainDrawTargets(
           *d.guest_vs_matrices,s.format.original_screen) ||
       !d.guest_shader_constants ||
       !selected->pipeline.original_vs_float_layout ||
       !selected->pipeline.original_ps_float_layout)
      return false;
    // Pack both stages BEFORE rendering. The shader's own verified register
    // bitmap determines the exact compacted XeFloatConstants vec4 stream;
    // never use a stale uniform buffer or a guessed float count.
    try {
      State::PackedConstants packed;
      packed.vertex=aot::PackOriginalXdkFloatConstants(
          *d.guest_shader_constants,
          *selected->pipeline.original_vs_float_layout,
          aot::Stage::Vertex,item.vertex_source_sha256);
      packed.pixel=aot::PackOriginalXdkFloatConstants(
          *d.guest_shader_constants,
          *selected->pipeline.original_ps_float_layout,
          aot::Stage::Pixel,item.pixel_source_sha256);
      if(s.frame.widescreen_camera &&
         selected->pipeline.original_view_space==
             VerifiedOriginalViewSpace::MainPerspective &&
         selected->pipeline.source_main_view) {
        const auto& camera=*selected->pipeline.source_main_view;
        packed.vertex=aot::ExpandVerifiedOriginalPackedViewProjection(
            packed.vertex,*selected->pipeline.original_vs_float_layout,
            camera.original_view_projection_first_register,
            camera.original_guest_aspect,
            s.frame.world.width,s.frame.world.height,camera.source_proof);
      }
      if(!prepared_constants.emplace(State::DrawKey{d.source_frame,
                        d.source_ordinal},std::move(packed)).second)
        return false;
    } catch (...) {return false;}
    // Reject duplicated/missing original SRVs BEFORE opening a host frame.
    // Previously two copies of one sampler passed this loop, then failed
    // BindOriginalSamplers only after earlier Draws had touched the target.
    if(!HasExactSourceSamplerBijection(
           item.original_samplers,selected->original_textures))
      return false;
    const auto mesh=s.draw_buffers.find(d.vertex_buffer);
    if(mesh==s.draw_buffers.end() ||
       mesh->second.original_model_hash!=item.original_model_hash ||
       item.original_mesh_header_offset==SIZE_MAX ||
       mesh->second.original_mesh_header_offset!=
           item.original_mesh_header_offset ||
       mesh->second.index_handle!=d.index_buffer ||
       mesh->second.host_vertex_slots.size()!=
           selected->pipeline.exact_host_input_layout.size() ||
       !s.meshes.IndexWindowInRange(d.index_buffer,d.first_index,
                                    d.index_count,d.base_vertex,
                                    mesh->second.vertex_count))
      return false;
    needed_materials.emplace(item.original_model_hash,
                             item.original_surface_ordinal,
                             item.effect_hash,item.pass_index);
  }
  // Pass two: compile/link only the ORIGINAL shader techniques that this
  // frame will execute. A PSO failure occurs before BeginScene and never
  // leaves earlier 3D objects partially submitted (red/yellow flashes).
  try {
    for(const auto& key:needed_materials) {
      if(s.pipeline_cache.contains(key))continue;
      const auto owner=s.material_lookup.find(key);
      if(owner==s.material_lookup.end())return false;
      auto pipeline=CreateOriginalVulkanPipeline(
          s.device.RawPtr(),s.shaders,s.bank,s.effects,
          owner->second->pipeline);
      s.pipeline_cache.emplace(key,std::move(pipeline));
    }
    // Pass three: build a distinct shader-resource-binding object for each
    // original material, before any GPU draw. An SRB is mutable and
    // must not leak albedo/normal samplers across two models that happen
    // to use the same compiled VS/PS technique.
    for(const auto& key:needed_materials) {
      if(s.material_bindings.contains(key))continue;
      const auto pipeline=s.pipeline_cache.find(key);
      if(pipeline==s.pipeline_cache.end() || !pipeline->second.pipeline)
        return false;
      const auto owner=s.material_lookup.find(key);
      if(owner==s.material_lookup.end())return false;
      const auto* original=owner->second;
      // CreateOriginalVulkanPipeline ALREADY creates one mutable SRB for
      // this exact original material owner. Reuse it rather than paying for
      // a second per-material Vulkan descriptor allocation on first frame.
      if(!pipeline->second.bindings)return false;
      auto binding=pipeline->second.bindings;
      BindVerifiedOriginalEorTextures(binding.RawPtr(),
                                       original->original_textures);
      s.material_bindings.emplace(key,std::move(binding));
    }
  } catch (...) {return false;}
  s.packed_constants=std::move(prepared_constants);
  s.frame_preflight_ready=true;
  return true;
}
bool DiligentEorDirectBackend::BeginScene(native::Size world,uint32_t samples) {
  auto& s=*state_;
  if(s.scene_open || !s.frame_preflight_ready ||
     !Same(world,s.frame.world) || samples!=s.frame.samples)
    return false;
  // Consume the provenance gate before touching any Vulkan render targets.
  s.frame_preflight_ready=false;
  if(!s.scene.Prepare(s.frame,s.format) || !s.scene.BeginScene())return false;
  s.scene_open=true;s.finished=false;s.selected=nullptr;
  s.source_timeline_active=false;
  s.source_resolve_recorded=false;
  s.source_present_recorded=false;
  s.composite_attempted=false;
  s.requested_model=0;s.requested_surface=UINT32_MAX;s.surface_ready=false;
  s.constants_ready=s.program_ready=s.textures_ready=false;
  return true;
}
void DiligentEorDirectBackend::AbortScene() noexcept {
  if(!state_)return;
  auto& s=*state_;
  s.scene.AbortScene();
  s.active={};
  s.selected=nullptr;s.scene_open=s.finished=false;
  s.frame_preflight_ready=false;
  s.packed_constants.clear();
  s.source_timeline_active=false;
  s.source_resolve_recorded=false;
  s.source_present_recorded=false;
  s.composite_attempted=false;
  s.requested_model=0;s.requested_surface=UINT32_MAX;s.surface_ready=false;
  s.constants_ready=s.program_ready=s.textures_ready=false;
}
bool DiligentEorDirectBackend::BindOriginalSurface(uint32_t model,uint32_t ordinal) {
  auto& s=*state_;
  // Every Draw begins from an unbound PSO/constant/sampler state.
  s.selected=nullptr;s.active={};
  s.constants_ready=s.program_ready=s.textures_ready=false;
  s.requested_model=0;s.requested_surface=UINT32_MAX;s.surface_ready=false;
  if(!s.scene_open || s.finished || !model || ordinal==UINT32_MAX)return false;
  // A source model may own many effects, but the first keyed pass is
  // sufficient to prove that THIS exact model/surface is resident.
  const auto item=s.material_lookup.lower_bound({model,ordinal,0u,0u});
  if(item==s.material_lookup.end() ||
     std::get<0>(item->first)!=model ||
     std::get<1>(item->first)!=ordinal)return false;
  s.requested_model=model;s.requested_surface=ordinal;
  s.surface_ready=true;
  return true;
}
bool DiligentEorDirectBackend::BindOriginalEffect(uint32_t effect,uint32_t pass) {
  auto& s=*state_;
  if(!s.scene_open || s.finished || !s.surface_ready || !effect ||
     pass==UINT32_MAX)return false;
  s.selected=nullptr;s.active={};
  s.constants_ready=s.program_ready=s.textures_ready=false;
  const auto item=s.material_lookup.find(
      {s.requested_model,s.requested_surface,effect,pass});
  if(item==s.material_lookup.end())return false;
  s.selected=item->second;
  return true;
}
bool DiligentEorDirectBackend::BindOriginalConstants(
    const aot::ReflectedShader& vs,const aot::ReflectedShader& ps) {
  auto& s=*state_;
  // A failed constant rebind must not leave the previous draw's verified
  // constants, program or samplers eligible for a subsequent DrawIndexed.
  s.constants_ready=false;
  s.program_ready=false;
  s.textures_ready=false;
  if(!s.selected || vs.stage!=aot::Stage::Vertex || ps.stage!=aot::Stage::Pixel)
    return false;
  s.vertex=vs;s.pixel=ps;
  s.constants_ready=true;
  return true; // actual frame constant values are bound per Draw by caller
}
bool DiligentEorDirectBackend::BindVerifiedProgramPair(
    const aot::Digest& vs_id,const aot::Blob& vs,
    const aot::Digest& ps_id,const aot::Blob& ps) {
  auto& s=*state_;
  s.program_ready=false;
  s.textures_ready=false;
  if(!s.selected || !s.constants_ready ||
     s.selected->pipeline.original_vertex_shader_sha256!=vs_id ||
     s.selected->pipeline.original_pixel_shader_sha256!=ps_id ||
     s.bank.Find(vs_id,aot::Stage::Vertex)!=&vs ||
     s.bank.Find(ps_id,aot::Stage::Pixel)!=&ps)
    return false;
  // This path executes in an open render pass: it must NOT compile a new
  // PSO or allocate a mutable SRB. Both were already source-preflighted.
  const auto pipeline=s.pipeline_cache.find({
      s.selected->original_model_hash,
      s.selected->original_surface_ordinal,
      s.selected->pipeline.original_effect_hash,
      s.selected->pipeline.original_pass_index});
  const auto binding=s.material_bindings.find({
      s.selected->original_model_hash,
      s.selected->original_surface_ordinal,
      s.selected->pipeline.original_effect_hash,
      s.selected->pipeline.original_pass_index});
  if(pipeline==s.pipeline_cache.end() ||
     binding==s.material_bindings.end() ||
     !pipeline->second.pipeline || !binding->second) {
    s.active={};
    return false;
  }
  s.active.pipeline=pipeline->second.pipeline;
  s.active.bindings=binding->second;
  s.program_ready=true;
  return true;
}
bool DiligentEorDirectBackend::BindOriginalSamplers(
    std::span<const VerifiedEorSampler> expected) {
  auto& s=*state_;
  s.textures_ready=false;
  if(!s.program_ready || !s.selected || !s.active.bindings)return false;
  // Never search other models with the same effect/technique. The
  // selected surface is exact even for shaders that sample zero textures.
  // Shader descriptor order and original material iteration order may differ.
  const auto& textures=s.selected->original_textures;
  if(!HasExactSourceSamplerBijection(expected,textures))return false;
  // All original GPU image variables and mip SRVs were actually bound
  // during PreflightOriginalScene. No Diligent Set() or GPU allocations
  // are allowed on the live Draw path.
  const auto found=s.material_bindings.find({
      s.selected->original_model_hash,
      s.selected->original_surface_ordinal,
      s.selected->pipeline.original_effect_hash,
      s.selected->pipeline.original_pass_index});
  if(found==s.material_bindings.end() ||
     found->second.RawPtr()!=s.active.bindings.RawPtr())
    return false;
  s.textures_ready=true;
  return true;
}
bool DiligentEorDirectBackend::DrawIndexed(const native::IndexedDraw& draw) {
  auto& s=*state_;
  // Per-Draw Vulkan constants/PSO/SRB are single-use. Consume even on a
  // rejected or throwing GPU submission; never reuse the previous object's
  // material or matrices if a caller omits the next original EoR binding.
  const bool source_bindings_ready =
      s.constants_ready && s.program_ready && s.textures_ready;
  s.constants_ready=s.program_ready=s.textures_ready=false;
  if(!s.scene_open || s.finished || !s.selected || !source_bindings_ready ||
     !s.active.pipeline ||
     !s.active.bindings || !draw.index_count || !draw.vertex_buffer ||
     !draw.index_buffer || s.meshes.KindOf(draw.index_buffer)!=2)
    return false;
  auto it=s.draw_buffers.find(draw.vertex_buffer);
  if(it==s.draw_buffers.end())return false;
  const auto& resident=it->second;
  if(resident.original_model_hash!=s.selected->original_model_hash ||
     resident.index_handle!=draw.index_buffer ||
     uint64_t(draw.first_index)+draw.index_count>resident.index_count)
    return false;
  // The signed base applies to THIS Draw's immutable index window, not
  // the whole source buffer (which may also contain other meshes).
  if(!s.meshes.IndexWindowInRange(draw.index_buffer,draw.first_index,
                                  draw.index_count,draw.base_vertex,
                                  resident.vertex_count))
    return false;
  const auto& slots=resident.host_vertex_slots;
  const auto& layout=s.selected->pipeline.exact_host_input_layout;
  if(slots.size()!=layout.size() || slots.empty() ||
     slots.size()>DILIGENT_MAX_LAYOUT_ELEMENTS || slots.size()>16)
    return false;
  // Every bound slot must be source-decoded; no unconverted BE attributes,
  // missing slots, or silent host default vertex layout.
  for(const auto& item:layout)
    if(item.BufferSlot>=slots.size() || !slots[item.BufferSlot])
      return false;
  std::array<Diligent::IBuffer*,16> buffers{};
  std::array<Diligent::Uint64,16> offsets{};
  for(size_t i=0;i<slots.size();++i) {
    if(s.meshes.KindOf(slots[i])!=(i==0?0u:3u))return false;
    buffers[i]=s.meshes.FindBuffer(slots[i]);
    if(!buffers[i])return false;
  }
  auto* index=s.meshes.FindBuffer(draw.index_buffer);
  if(!index || uint64_t(draw.first_index)+draw.index_count>
                   index->GetDesc().Size/sizeof(uint32_t))
    return false;
  try {
    // A live native EoR Draw must bind the source guest matrices as typed
    // verified values for this exact frame/call before touching GPU state.
    // Shader-specific SPIR-V UBO layouts are an independent provenance gate.
    const auto matrices=aot::ExtractOriginalDrawVsMatrices(s.vertex,draw);
    const auto packed=s.packed_constants.find({
        draw.source_frame,draw.source_ordinal});
    if(!matrices || !draw.guest_shader_constants ||
       packed==s.packed_constants.end() ||
       !s.bind_constants(s.active.bindings.RawPtr(),s.vertex,s.pixel,
                         draw,*matrices,*draw.guest_shader_constants,
                         packed->second.vertex,packed->second.pixel)) {
      // Never permit a later draw to reuse constants from an earlier draw
      // after a partially failed host constant-buffer update.
      s.constants_ready=false;
      s.program_ready=false;
      s.textures_ready=false;
      return false;
    }
    if(s.frame.widescreen_camera) {
      // Independent native aspect pass layout: world covers all physical
      // pixels; verified HUD draws only to the centered original 16:9
      // host area. Reset state on EVERY draw to prevent a HUD viewport
      // leaking into the next original 3D draw or vice versa.
      native::Rect v{0,0,s.frame.world.width,s.frame.world.height};
      if(s.selected->pipeline.original_view_space==
           VerifiedOriginalViewSpace::OriginalAspectHud) {
        const auto fitted=display::FitOriginalPicture(
            s.frame.world.width,s.frame.world.height,
            s.frame.original_guest.width,s.frame.original_guest.height);
        if(!fitted.valid || !fitted.width || !fitted.height)
          return false;
        v={fitted.x,fitted.y,fitted.width,fitted.height};
      }
      const Diligent::Viewport viewport{
          static_cast<float>(v.left),static_cast<float>(v.top),
          static_cast<float>(v.width),static_cast<float>(v.height),
          0.f,1.f};
      const Diligent::Rect scissor{
          static_cast<int>(v.left),static_cast<int>(v.top),
          static_cast<int>(v.left+v.width),
          static_cast<int>(v.top+v.height)};
      s.context->SetViewports(1,&viewport,
                            s.frame.world.width,s.frame.world.height);
      s.context->SetScissorRects(1,&scissor,
                               s.frame.world.width,s.frame.world.height);
    }
    s.context->SetPipelineState(s.active.pipeline.RawPtr());
    s.context->CommitShaderResources(s.active.bindings.RawPtr(),
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    s.context->SetVertexBuffers(0,static_cast<Diligent::Uint32>(slots.size()),
        buffers.data(),offsets.data(),
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    s.context->SetIndexBuffer(index,0,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DrawIndexedAttribs command(
        draw.index_count,Diligent::VT_UINT32,Diligent::DRAW_FLAG_NONE,
        1,draw.first_index,static_cast<Diligent::Uint32>(draw.base_vertex));
    s.context->DrawIndexed(command);
    return true;
  } catch (...) {
    // A Diligent exception can occur after a partial resource/constant bind.
    // Do not carry that state into another draw without explicit revalidation.
    s.constants_ready=false;
    s.program_ready=false;
    s.textures_ready=false;
    return false;
  }
}
bool DiligentEorDirectBackend::ResolveScene(native::Size physical) {
  auto& s=*state_;
  if(!s.scene_open || s.finished || !Same(physical,s.frame.world) ||
     (s.source_timeline_active && !s.source_resolve_recorded))return false;
  // A live source frame must reach its verified Resolve event before the
  // host Vulkan resolve. Offline anonymous scenes have no XDK timeline.
  if(!s.scene.ResolveScene())return false;
  s.finished=true;return true;
}
bool DiligentEorDirectBackend::Composite(native::Rect dest,native::Size output) {
  auto& s=*state_;
  if(!s.scene_open || !s.finished || s.composite_attempted ||
     (s.source_timeline_active && !s.source_present_recorded) ||
     !Same(dest,s.frame.composite) || !Same(output,s.frame.backbuffer))
    return false;
  auto* image=s.scene.ResolvedColorSRV();
  if(!image)return false;
  // Present may have been queued even if the callback throws or reports a
  // lost device. Consume authorization before entering external GPU code:
  // retrying the same original Present could submit a second host frame.
  s.composite_attempted=true;
  if(!s.composite(image,dest,output))return false;
  s.active={};s.selected=nullptr;s.scene_open=false;
  s.requested_model=0;s.requested_surface=UINT32_MAX;s.surface_ready=false;
  return true;
}
} // namespace hulk::eor::gpu
