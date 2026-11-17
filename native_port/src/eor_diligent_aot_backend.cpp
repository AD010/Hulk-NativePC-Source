#include "eor_diligent_aot_backend.h"
#include "eor_spirv_texture_bindings.h"
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
  // Immutable shader/PSO pairing remains live across Draws and frames;
  // no per-draw driver compilation and no guessed shader cache keys.
  std::map<std::pair<uint32_t,uint32_t>,OriginalVulkanPipeline> pipeline_cache;
  bool frame_preflight_ready=false;
  bool scene_open=false,constants_ready=false,program_ready=false;
  bool textures_ready=false,finished=false;
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
    for(size_t j=0;j<i;++j) {
      const auto& earlier=state_->passes[j];
      const auto& old=earlier.pipeline;
      // Shader PSO is shared by many surfaces, but NEVER the material's
      // texture identity. Reject a duplicate owner and a shared PSO
      // key whose fixed-function state or host vertex layout differs.
      if(earlier.original_model_hash==owner.original_model_hash &&
         earlier.original_surface_ordinal==owner.original_surface_ordinal &&
         old.original_effect_hash==pass.original_effect_hash &&
         old.original_pass_index==pass.original_pass_index)
        throw std::invalid_argument("Duplicate native EoR surface/material pass");
      if(old.original_effect_hash==pass.original_effect_hash &&
         old.original_pass_index==pass.original_pass_index &&
         (old.original_vertex_shader_sha256!=pass.original_vertex_shader_sha256 ||
          old.original_pixel_shader_sha256!=pass.original_pixel_shader_sha256 ||
          !(old.exact_render_state==pass.exact_render_state) ||
          old.exact_host_input_layout!=pass.exact_host_input_layout))
        throw std::invalid_argument("Shared native EoR pass has conflicting PSO state");
    }
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
       state_->meshes.KindOf(resident.index_handle)!=2 ||
       state_->meshes.MaxIndexOf(resident.index_handle)>=resident.vertex_count)
      throw std::invalid_argument("Missing or out-of-range EoR host mesh vertex/index provenance");
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
}
DiligentEorDirectBackend::~DiligentEorDirectBackend() noexcept {AbortScene();}
bool DiligentEorDirectBackend::PreflightOriginalScene(
    std::span<const aot::BoundIndexedDraw> draws) {
  auto& s=*state_;
  s.frame_preflight_ready=false;
  if(s.scene_open || draws.empty())return false;
  std::set<std::pair<uint32_t,uint32_t>> needed;
  // Pass one: validate the ENTIRE frame's native draw and original material
  // ownership. Do not create a PSO before a later invalid draw is rejected.
  for(const auto& item:draws) {
    const auto& d=item.draw;
    if(!d.vertex_buffer || !d.index_buffer || !d.vertex_shader ||
       !d.pixel_shader || !d.index_count || !item.original_model_hash ||
       item.original_surface_ordinal==UINT32_MAX || !item.effect_hash ||
       item.pass_index==UINT32_MAX)
      return false;
    const NativeEorPassResources* selected=nullptr;
    for(const auto& material:s.passes) {
      if(material.original_model_hash==item.original_model_hash &&
         material.original_surface_ordinal==item.original_surface_ordinal &&
         material.pipeline.original_effect_hash==item.effect_hash &&
         material.pipeline.original_pass_index==item.pass_index) {
        selected=&material;
        break;
      }
    }
    if(!selected ||
       selected->pipeline.original_vertex_shader_sha256!=item.vertex_source_sha256 ||
       selected->pipeline.original_pixel_shader_sha256!=item.pixel_source_sha256 ||
       selected->original_textures.size()!=item.original_samplers.size())
      return false;
    for(const auto& sampler:item.original_samplers) {
      unsigned matches=0;
      for(const auto& owned:selected->original_textures)
        if(Same(sampler,owned.identity))++matches;
      if(matches!=1)return false;
    }
    const auto mesh=s.draw_buffers.find(d.vertex_buffer);
    if(mesh==s.draw_buffers.end() ||
       mesh->second.original_model_hash!=item.original_model_hash ||
       mesh->second.index_handle!=d.index_buffer ||
       mesh->second.host_vertex_slots.size()!=
           selected->pipeline.exact_host_input_layout.size() ||
       !s.meshes.IndexWindowInRange(d.index_buffer,d.first_index,
                                    d.index_count,d.base_vertex,
                                    mesh->second.vertex_count))
      return false;
    needed.emplace(item.effect_hash,item.pass_index);
  }
  // Pass two: compile/link only the ORIGINAL shader techniques that this
  // frame will execute. A PSO failure occurs before BeginScene and never
  // leaves earlier 3D objects partially submitted (red/yellow flashes).
  try {
    for(const auto& key:needed) {
      if(s.pipeline_cache.contains(key))continue;
      const NativeEorPassResources* match=nullptr;
      for(const auto& material:s.passes)
        if(material.pipeline.original_effect_hash==key.first &&
           material.pipeline.original_pass_index==key.second) {
          match=&material;
          break;
        }
      if(!match)return false;
      auto pipeline=CreateOriginalVulkanPipeline(
          s.device.RawPtr(),s.shaders,s.bank,s.effects,match->pipeline);
      s.pipeline_cache.emplace(key,std::move(pipeline));
    }
  } catch (...) {return false;}
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
  for(const auto& item:s.passes)
    if(item.original_model_hash==model && item.original_surface_ordinal==ordinal) {
      s.requested_model=model;s.requested_surface=ordinal;
      s.surface_ready=true;return true;
    }
  return false;
}
bool DiligentEorDirectBackend::BindOriginalEffect(uint32_t effect,uint32_t pass) {
  auto& s=*state_;
  if(!s.scene_open || s.finished || !s.surface_ready || !effect ||
     pass==UINT32_MAX)return false;
  s.selected=nullptr;s.active={};
  s.constants_ready=s.program_ready=s.textures_ready=false;
  for(const auto& item:s.passes)
    if(item.original_model_hash==s.requested_model &&
       item.original_surface_ordinal==s.requested_surface &&
       item.pipeline.original_effect_hash==effect &&
       item.pipeline.original_pass_index==pass) {
      // Duplicate exact surface/pass owners were rejected by the constructor.
      s.selected=&item;return true;
    }
  return false;
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
  try {
    const auto key=std::make_pair(
        s.selected->pipeline.original_effect_hash,
        s.selected->pipeline.original_pass_index);
    auto cached=s.pipeline_cache.find(key);
    if(cached==s.pipeline_cache.end()) {
      auto newly_created=CreateOriginalVulkanPipeline(
          s.device.RawPtr(),s.shaders,s.bank,s.effects,s.selected->pipeline);
      cached=s.pipeline_cache.emplace(key,std::move(newly_created)).first;
    }
    s.active=cached->second;
    s.program_ready=true;
    return true;
  } catch (...) {s.active={};return false;}
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
  if(expected.size()!=textures.size())return false;
  // Require a bijection, not merely that each requested sampler exists.
  // Repeating one valid sampler could otherwise omit another original image
  // while still passing the size and per-entry identity checks.
  std::set<size_t> matched_indices;
  for(const auto& slot:expected) {
    size_t match=textures.size();
    for(size_t i=0;i<textures.size();++i)
      if(Same(slot,textures[i].identity)) {
        if(match!=textures.size())return false;
        match=i;
      }
    if(match==textures.size() || !matched_indices.insert(match).second)
      return false;
  }
  try {
    BindVerifiedOriginalEorTextures(s.active.bindings.RawPtr(),textures);
    s.textures_ready=true;
    return true;
  } catch (...) {return false;}
}
bool DiligentEorDirectBackend::DrawIndexed(const native::IndexedDraw& draw) {
  auto& s=*state_;
  if(!s.scene_open || s.finished || !s.selected || !s.constants_ready ||
     !s.program_ready || !s.textures_ready || !s.active.pipeline ||
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
     slots.size()>DILIGENT_MAX_LAYOUT_ELEMENTS)
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
    s.context->SetPipelineState(s.active.pipeline.RawPtr());
    if(!s.bind_constants(s.active.bindings.RawPtr(),s.vertex,s.pixel,draw)) {
      // Never permit a later draw to reuse constants from an earlier draw
      // after a partially failed host constant-buffer update.
      s.constants_ready=false;
      s.program_ready=false;
      s.textures_ready=false;
      return false;
    }
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
  if(!s.scene_open || s.finished || !Same(physical,s.frame.world))return false;
  if(!s.scene.ResolveScene())return false;
  s.finished=true;return true;
}
bool DiligentEorDirectBackend::Composite(native::Rect dest,native::Size output) {
  auto& s=*state_;
  if(!s.scene_open || !s.finished ||
     !Same(dest,s.frame.composite) || !Same(output,s.frame.backbuffer))
    return false;
  auto* image=s.scene.ResolvedColorSRV();
  if(!image || !s.composite(image,dest,output))return false;
  s.active={};s.selected=nullptr;s.scene_open=false;
  s.requested_model=0;s.requested_surface=UINT32_MAX;s.surface_ready=false;
  return true;
}
} // namespace hulk::eor::gpu
