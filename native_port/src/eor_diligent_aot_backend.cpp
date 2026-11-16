#include "eor_diligent_aot_backend.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Buffer.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"
#include "TextureView.h"
#include "RefCntAutoPtr.hpp"
#include <algorithm>
#include <array>
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
      if(!draw_buffers.emplace(entry.position_handle,std::move(entry.host_vertex_slots)).second)
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
  std::map<uint64_t,std::vector<uint64_t>> draw_buffers;
  BindConstants bind_constants;
  CompositeOutput composite;
  const NativeEorPassResources* selected=nullptr;
  aot::ReflectedShader vertex{},pixel{};
  OriginalVulkanPipeline active{};
  // Immutable shader/PSO pairing remains live across Draws and frames;
  // no per-draw driver compilation and no guessed shader cache keys.
  std::map<std::pair<uint32_t,uint32_t>,OriginalVulkanPipeline> pipeline_cache;
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
     !state_->composite || !bank.HasFullArchiveCoverage() ||
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
    const auto& pass=state_->passes[i].pipeline;
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
      const auto& old=state_->passes[j].pipeline;
      if(old.original_effect_hash==pass.original_effect_hash &&
         old.original_pass_index==pass.original_pass_index)
        throw std::invalid_argument("Ambiguous native original effect/pass definition");
    }
  }
  for(const auto& [position,slots]:state_->draw_buffers) {
    if(!position || slots.empty() || slots.size()>16 ||
       slots[0]!=position || state_->meshes.KindOf(position)!=0)
      throw std::invalid_argument("Missing EoR decoded host position buffer");
    for(size_t i=1;i<slots.size();++i)
      if(!slots[i] || state_->meshes.KindOf(slots[i])!=3)
        throw std::invalid_argument("Raw Xbox BE vertex bytes cannot bind to host Vulkan");
  }
}
DiligentEorDirectBackend::~DiligentEorDirectBackend() noexcept {AbortScene();}
bool DiligentEorDirectBackend::BeginScene(native::Size world,uint32_t samples) {
  auto& s=*state_;
  if(s.scene_open || !Same(world,s.frame.world) || samples!=s.frame.samples)
    return false;
  if(!s.scene.Prepare(s.frame,s.format) || !s.scene.BeginScene())return false;
  s.scene_open=true;s.finished=false;s.selected=nullptr;
  s.constants_ready=s.program_ready=s.textures_ready=false;
  return true;
}
void DiligentEorDirectBackend::AbortScene() noexcept {
  if(!state_)return;
  auto& s=*state_;
  s.scene.AbortScene();
  s.active={};
  s.selected=nullptr;s.scene_open=s.finished=false;
  s.constants_ready=s.program_ready=s.textures_ready=false;
}
bool DiligentEorDirectBackend::BindOriginalEffect(uint32_t effect,uint32_t pass) {
  auto& s=*state_;
  if(!s.scene_open || s.finished)return false;
  s.selected=nullptr;s.active={};
  s.constants_ready=s.program_ready=s.textures_ready=false;
  for(const auto& item:s.passes)
    if(item.pipeline.original_effect_hash==effect &&
       item.pipeline.original_pass_index==pass) {
      s.selected=&item;return true;
    }
  return false;
}
bool DiligentEorDirectBackend::BindOriginalConstants(
    const aot::ReflectedShader& vs,const aot::ReflectedShader& ps) {
  auto& s=*state_;
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
  if(!s.program_ready || !s.selected || !s.active.bindings ||
     expected.size()!=s.selected->original_textures.size())
    return false;
  for(size_t i=0;i<expected.size();++i)
    if(!Same(expected[i],s.selected->original_textures[i].identity))
      return false;
  try {
    BindVerifiedOriginalEorTextures(s.active.bindings.RawPtr(),
                                   s.selected->original_textures);
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
  const auto& slots=it->second;
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
  if(!index)return false;
  try {
    s.context->SetPipelineState(s.active.pipeline.RawPtr());
    if(!s.bind_constants(s.active.bindings.RawPtr(),s.vertex,s.pixel))
      return false;
    s.context->CommitShaderResources(s.active.bindings.RawPtr(),
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    s.context->SetVertexBuffers(0,static_cast<Diligent::Uint32>(slots.size()),
        buffers.data(),offsets.data(),
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    s.context->SetIndexBuffer(index,0,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DrawIndexedAttribs command(
        draw.index_count,Diligent::VT_UINT32,Diligent::DRAW_FLAG_NONE,
        1,draw.first_index);
    s.context->DrawIndexed(command);
    return true;
  } catch (...) {return false;}
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
  return true;
}
} // namespace hulk::eor::gpu
