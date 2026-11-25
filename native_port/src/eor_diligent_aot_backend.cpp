#include "eor_diligent_aot_backend.h"
#include "eor_live_projection.h"
#include "eor_original_font_source.h"
#include "eor_host_vertex_layout.h"
#include "eor_spirv_texture_bindings.h"
#include "native_resolution.h"
#include "eor_native_ui_prompts.h"
#include "eor_ui_original_dds.h"
#include <cstdlib>
#include <filesystem>
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Buffer.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"
#include "TextureView.h"
#include "RefCntAutoPtr.hpp"
#include "eor_original_resolve_reuse.h"
#include <algorithm>
#include <array>
#include <bit>
#include <set>
#include <tuple>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace hulk::eor::gpu {
namespace {
using MaterialKey=std::tuple<uint32_t,uint32_t,uint32_t,uint32_t,bool,bool,uint32_t>;
// Exact fixed-function variants of a single verified material. The source
// register tuple includes every input of DeriveOriginalXdkGraphicsPipelineState;
// it is intentionally not a lossy hash or the guest shader-object address.
using PipelineVariantKey=std::pair<MaterialKey,std::array<uint32_t,11>>;
std::optional<PipelineVariantKey> OriginalPipelineVariantKey(
    const MaterialKey& material,const xdk::OriginalXdkFixedGraphicsState& source) {
  constexpr std::array<uint32_t,11> registers{
      0x2000u,0x2001u,0x2002u,0x2104u,0x210cu,0x210du,0x2200u,0x2201u,0x2202u,0x2204u,0x2205u};
  PipelineVariantKey key{};
  key.first=material;
  for(size_t i=0;i<registers.size();++i) {
    const auto value=source.ReadOriginalXenosRegister(registers[i]);
    if(!value)return std::nullopt;
    key.second[i]=*value;
  }
  return key;
}
MaterialKey PassKey(uint32_t model,uint32_t surface,const VerifiedOriginalGraphicsPass& pass) {
  return {model,surface,pass.original_effect_hash,pass.original_pass_index,pass.original_depth_unbound,pass.original_runtime_draw_verified,pass.original_color_owner};
}
MaterialKey PassKey(const NativeEorPassResources& owner) {
  return PassKey(owner.original_model_hash,owner.original_surface_ordinal,owner.pipeline);
}
bool Same(native::Size a,native::Size b) {
  return a.width==b.width && a.height==b.height;
}
bool Same(native::Rect a,native::Rect b) {
  return a.left==b.left && a.top==b.top &&
         a.width==b.width && a.height==b.height;
}
bool Same(const VerifiedEorSampler& a,const VerifiedEorSampler& b) {
  return a.original_runtime_material==b.original_runtime_material && a.original_model_hash==b.original_model_hash &&
         a.original_surface_ordinal==b.original_surface_ordinal &&
         a.original_effect_hash==b.original_effect_hash &&
         a.original_pass_index==b.original_pass_index &&
         a.original_texture_hash==b.original_texture_hash &&
         a.original_resolved_guest_object==b.original_resolved_guest_object &&
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
// Validate actual byte-addressed host vertex allocation before accepting
// source-mapped shader attributes. Diligent AUTO_OFFSET and AUTO_STRIDE must
// resolve within the real float3 position / float4 attribute GPU buffers.
// Sharing a slot cannot make two source attributes alias the same bytes.
bool HasVerifiedHostAttributeLayout(
    std::span<const Diligent::LayoutElement> layout) {
  if(layout.empty() || layout.size()>DILIGENT_MAX_LAYOUT_ELEMENTS ||
     layout.size()>32)return false;
  std::array<HostVertexAttribute,DILIGENT_MAX_LAYOUT_ELEMENTS> host{};
  for(size_t i=0;i<layout.size();++i) {
    const auto& item=layout[i];
    host[i]={static_cast<uint32_t>(item.BufferSlot),
             static_cast<uint32_t>(item.NumComponents),
             static_cast<uint32_t>(item.RelativeOffset),
             static_cast<uint32_t>(item.Stride),
             // Scalar XenosRecomp NORMAL/TANGENT inputs are uint4 bit
             // patterns of the same native float4 bytes (asfloat path).
             item.ValueType==Diligent::VT_FLOAT32 ||
               (item.ValueType==Diligent::VT_UINT32 && item.BufferSlot>0 &&
                (item.InputIndex==1u || item.InputIndex==2u || item.InputIndex==9u) &&
                item.NumComponents==4u),
             item.Frequency==Diligent::INPUT_ELEMENT_FREQUENCY_PER_VERTEX};
  }
  return ValidateHostVertexLayout(
      std::span<const HostVertexAttribute>(host.data(),layout.size()),
      Diligent::LAYOUT_ELEMENT_AUTO_OFFSET,
      Diligent::LAYOUT_ELEMENT_AUTO_STRIDE);
}
// Attribute count is distinct from GPU stream count: multiple nonoverlapping
// declared inputs may share a single decoded float4 source buffer.
// Original EFontShader draws upload independent native FLOAT4 slots for
// position/UV/color. A static Xbox mesh's slot-0 XYZ12 interpretation
// must NEVER be reused for these 2D attributes.
uint32_t SourceFontSlotMask(
    std::span<const Diligent::LayoutElement> layout,
    std::span<const uint64_t> slots) {
  if(layout.size()!=slots.size() || layout.size()<2 ||
     layout.size()>3)return 0;
  uint32_t mask=0;
  for(const auto& item:layout) {
    if(item.BufferSlot>=slots.size() || !slots[item.BufferSlot] ||
       (mask & (1u<<item.BufferSlot)) ||
       item.ValueType!=Diligent::VT_FLOAT32 ||
       item.NumComponents<2 || item.NumComponents>4 ||
       (item.RelativeOffset!=0 &&
        item.RelativeOffset!=Diligent::LAYOUT_ELEMENT_AUTO_OFFSET) ||
       (item.Stride!=16u &&
        item.Stride!=Diligent::LAYOUT_ELEMENT_AUTO_STRIDE) ||
       item.Frequency!=Diligent::INPUT_ELEMENT_FREQUENCY_PER_VERTEX)
      return 0;
    mask |= 1u<<item.BufferSlot;
  }
  return mask==(1u<<slots.size())-1u ? mask:0u;
}
uint32_t SourceRuntimeSlotMask(std::span<const Diligent::LayoutElement> layout,
                               std::span<const uint64_t> slots) {
  if(layout.empty() || layout.size()!=slots.size() || slots.size()>16)return 0;
  uint32_t mask=0,locations=0;
  for(const auto& e:layout) {
    if(e.BufferSlot>=slots.size() || !slots[e.BufferSlot] || e.InputIndex>=16u ||
       (mask&(1u<<e.BufferSlot)) || (locations&(1u<<e.InputIndex)) ||
       e.NumComponents!=4u || e.Stride!=16u || e.RelativeOffset!=0 ||
       e.Frequency!=Diligent::INPUT_ELEMENT_FREQUENCY_PER_VERTEX ||
       (e.ValueType!=Diligent::VT_FLOAT32 &&
        !(e.ValueType==Diligent::VT_UINT32 && (e.InputIndex==1u || e.InputIndex==2u || e.InputIndex==9u))))return 0;
    mask|=1u<<e.BufferSlot;locations|=1u<<e.InputIndex;
  }
  return (locations&1u)?mask:0u;
}
uint32_t SourceVertexSlotMask(
    std::span<const Diligent::LayoutElement> layout,
    std::span<const uint64_t> slots) {
  if(slots.empty() || slots.size()>16 || !HasVerifiedHostAttributeLayout(layout))
    return 0;
  uint32_t mask=0;
  for(const auto& element:layout) {
    if(element.BufferSlot>=slots.size() || !slots[element.BufferSlot])
      return 0;
    mask |= 1u << element.BufferSlot;
  }
  return mask;
}
}
std::vector<NativeEorPassResources> AcquireVerifiedNativeMaterialPasses(
    std::span<const VerifiedNativeMaterialPass> verified,
    DiligentOriginalTextureResidency& residency) {
  if(verified.empty())
    throw std::invalid_argument("No original source-proven EoR material passes");
  std::set<MaterialKey> keys;
  // Validate every material/pass owner BEFORE the first original GPU image
  // allocation; never replace a missing Texture with a synthetic SRV.
  for(const auto& p:verified) {
    const auto& pass=p.original_pipeline;
    if(!p.original_model_hash || p.original_surface_ordinal==UINT32_MAX ||
       !pass.original_effect_hash || pass.original_pass_index==UINT32_MAX ||
       !pass.material_pass_verified || !pass.original_render_state_verified ||
       !pass.original_host_vertex_layout_verified ||
       !keys.insert(PassKey(p.original_model_hash,p.original_surface_ordinal,pass)).second)
      throw std::invalid_argument("Ambiguous or unverified original EoR material pass");
    std::vector<VerifiedEorSampler> identifiers;
    identifiers.reserve(p.original_images.size());
    for(const auto& image:p.original_images) {
      const auto& owner=image.original_binding;
      if(owner.original_runtime_material!=pass.original_runtime_draw_verified ||
         owner.original_model_hash!=p.original_model_hash ||
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
  State(Diligent::IRenderDevice* d,Diligent::IDeviceContext* c,
        DiligentSceneTargets& sc,DiligentMeshUploadBackend& ms,
        DiligentAotShaderModules& shaders,const aot::ShaderBank& bank,
        const aot::EffectCatalog& effects,native::FrameGeometry f,
        VerifiedSceneFormat formats,std::vector<NativeEorPassResources> source_passes,
        std::vector<NativeEorDrawBuffers> buffers,BindConstants bind,
        CompositeOutput present)
      :device(d),context(c),scene(sc),meshes(ms),shaders(shaders),
       bank(bank),effects(effects),frame(f),format(formats),
       passes(std::move(source_passes)),bind_constants(std::move(bind)),
       composite(std::move(present)) {
    // Pre-index exact source model/surface/effect/pass ownership ONCE at
    // asset load, not O(draws * all-materials) on every game frame.
    // Pointers remain stable: the resident pass vector is never reallocated.
    for(const auto& original:passes) {
      const auto key=PassKey(original);
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
  std::map<MaterialKey,std::unique_ptr<NativeEorPassResources>> streamed_passes;
  std::map<uint64_t,NativeEorDrawBuffers> draw_buffers;
  BindConstants bind_constants;
  CompositeOutput composite;
  const NativeEorPassResources* selected=nullptr;
  uint32_t requested_model=0, requested_surface=UINT32_MAX;
  bool requested_runtime=false;
  bool surface_ready=false;
  aot::ReflectedShader vertex{},pixel{};
  OriginalVulkanPipeline active{};
  // Shader MODULES are cached by digest, but host PSOs are keyed by the
  // EXACT original model/surface/effect/pass: two materials may share a
  // ShaderEffect pair yet use different source-proven blend/depth/cull or
  // vertex input state. Sharing a PSO keyed by technique alone was unsafe.
  std::map<MaterialKey,const NativeEorPassResources*> material_lookup;
  using PromptKey=std::tuple<uint32_t,std::string,uint32_t,uint32_t,Diligent::TEXTURE_FORMAT>;
  std::map<PromptKey,Diligent::RefCntAutoPtr<Diligent::ITexture>> pc_prompt_images;
  int32_t world_anisotropy=-1;
  std::map<std::string,std::string,std::less<>> pc_bindings;
  OriginalResolveReuse resolve_reuse;
  Diligent::ITextureView* AcquirePcPrompt(const UploadedEorTexture& source,
      const VerifiedOriginalGraphicsPass& pass,const std::array<uint32_t,6>& fetch) noexcept {
    try {
      using namespace Diligent;
      if(!pass.original_view_space_verified ||
         pass.original_view_space!=VerifiedOriginalViewSpace::OriginalAspectHud ||
         !pass.original_hud_clip_coordinates_verified ||
         pc_bindings.empty() ||
         source.view_kind!=OriginalTextureViewKind::Native2D ||
         !source.host_texture || source.original_mip_levels!=1u ||
         (fetch[4]>>2&255u) || (fetch[5]>>12&0x1ffffu))return nullptr;
      const auto key_name=ui::PcPromptBinding(source.original_texture_hash);
      if(key_name.empty())return nullptr;
      const auto binding=pc_bindings.find(key_name);
      if(binding==pc_bindings.end())return nullptr;
      const auto token=ui::FirstPromptToken(binding->second);
      if(token.empty())return nullptr;
      const auto& original=source.host_texture->GetDesc();
      if(original.MipLevels!=1u || original.ArraySize!=1u ||
         original.Width<32 || original.Height<16 || original.Width>256 ||
         original.Height>256 ||
         (original.Format!=TEX_FORMAT_RGBA8_UNORM && original.Format!=TEX_FORMAT_BGRA8_UNORM))
        return nullptr;
      const PromptKey key{source.original_texture_hash,std::string(token),
          original.Width,original.Height,original.Format};
      if(const auto it=pc_prompt_images.find(key);it!=pc_prompt_images.end())
        return it->second?it->second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE):nullptr;
      // Limit cache ownership. Current Draw SRBs retain their own references
      // independently; dropping a cache entry does not destroy a live image.
      if(pc_prompt_images.size()>=128u)pc_prompt_images.clear();
      std::vector<uint8_t> pixels;
      const auto asset=ui::KnownPcPromptAsset(token);
      if(!asset.empty()) {
        try {
          const char* root=std::getenv("HULK_PC2008_UI_DIR");
          const auto folder=root&&*root?std::filesystem::path(root):std::filesystem::current_path();
          auto archive=folder/"textures(1).arc";
          if(!std::filesystem::is_regular_file(archive))archive=folder/"TEXTURES.ARC";
          const auto image=ui::LoadPcMenuArt(folder/"indexPC.ind",archive,asset);
          pixels=ui::FitPcPromptImage(image.rgba,image.width,image.height,
                                      original.Width,original.Height);
        } catch (...) {}
      }
      if(pixels.empty())pixels=ui::RasterPcKeycap(token,original.Width,original.Height);
      if(pixels.empty())return nullptr;
      if(original.Format==TEX_FORMAT_BGRA8_UNORM)
        for(size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
      TextureDesc desc{};
      desc.Name="Native PC key in original EoR UI material";
      desc.Type=RESOURCE_DIM_TEX_2D;desc.Format=original.Format;
      desc.Width=original.Width;desc.Height=original.Height;
      desc.ArraySize=1;desc.MipLevels=1;desc.SampleCount=1;
      desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_SHADER_RESOURCE;
      TextureSubResData data{};data.pData=pixels.data();data.Stride=original.Width*4u;
      TextureData initial{};initial.pSubResources=&data;initial.NumSubresources=1;
      RefCntAutoPtr<ITexture> texture;
      device->CreateTexture(desc,&initial,&texture);
      auto view=texture?texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE):nullptr;
      if(!view)return nullptr;
      pc_prompt_images.emplace(key,std::move(texture));
      return view;
    } catch (...) {return nullptr;}
  }
  std::map<MaterialKey,OriginalVulkanPipeline> pipeline_cache;
  std::optional<MaterialKey> prewarm_cursor;
  // The SAME authenticated surface/effect can legitimately change cull,
  // color write, depth and blend state between original Draw calls.
  std::map<PipelineVariantKey,OriginalVulkanPipeline> pipeline_variants;
  // Each original pass owns a unique mutable Vulkan descriptor binding.
  std::map<MaterialKey,Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>>
      material_bindings;
  // Per actual guest frame/ordinal, with no shader remapping or inter-Draw
  // state inheritance. Only source-proven translator-used floats are packed.
  using DrawKey=std::pair<uint64_t,uint32_t>;
  // Original EFontShader writes a transient VB generation each frame.
  // Keep exact immutable native float4/index32 allocations until the
  // host GPU fence has COMPLETED, never while its Draw may be in flight.
  std::map<DrawKey,ui::UploadedLiveFontGeometry> font_uploads;
  struct RuntimeUpload {
    DiligentMeshUploadBackend* gpu=nullptr;
    std::vector<uint64_t> handles;
    MaterialKey material{};
    uint64_t position=0;
    ~RuntimeUpload(){if(gpu)for(uint64_t h:handles)if(h)gpu->ReleaseBuffer(h);}
  };
  std::map<DrawKey,std::unique_ptr<RuntimeUpload>> runtime_uploads;
  struct PackedConstants {
    std::vector<float> vertex,pixel;
    FloatRegisterAbi vs_abi=FloatRegisterAbi::External,ps_abi=FloatRegisterAbi::External;
    native::HostRasterWindow raster{};
    std::optional<PipelineVariantKey> variant;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> variant_pipeline;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> draw_bindings;
  };
  std::map<DrawKey,PackedConstants> packed_constants;
  bool frame_preflight_ready=false;
  bool first_draw_depth_bound=false;
  bool scene_open=false,constants_ready=false,program_ready=false;
  bool textures_ready=false,finished=false;
  bool source_timeline_active=false;
  bool source_resolve_recorded=false;
  bool source_present_recorded=false;
  // One source frame can authorize at most one physical host Present attempt.
  bool composite_attempted=false;
};
// Startup and late-streamed original materials use the SAME render-state,
// source shader SPIR-V, original sampler/image and input-layout proof.
static void ValidateSourceOriginalMaterialPass(
    NativeEorPassResources& owner,const aot::ShaderBank& bank,
    const aot::EffectCatalog& effects,const native::FrameGeometry& frame,
    const VerifiedSceneFormat& formats) {
  const auto target=owner.pipeline.original_color_owner;
  if(formats.original_graph.active() && !formats.original_graph.HasColor(target))
    throw std::invalid_argument("Original material target owner absent from graph");
  const auto native_color=formats.original_graph.ColorFormat(target,formats.color);
  const auto samples=formats.original_graph.Samples(target,frame.samples);
  const Diligent::TEXTURE_FORMAT color =
      native_color==NativeColorFormat::Rgba8 ?
          Diligent::TEX_FORMAT_RGBA8_UNORM :
      native_color==NativeColorFormat::Rgba16Float ?
          Diligent::TEX_FORMAT_RGBA16_FLOAT : Diligent::TEX_FORMAT_UNKNOWN;
  const Diligent::TEXTURE_FORMAT depth =
      formats.depth==NativeDepthFormat::D24S8 ?
          Diligent::TEX_FORMAT_D24_UNORM_S8_UINT :
      formats.depth==NativeDepthFormat::D32Float ?
          Diligent::TEX_FORMAT_D32_FLOAT : Diligent::TEX_FORMAT_UNKNOWN;
    auto& pass=owner.pipeline;
    // Decoded attribute buffers keep a float4 per vertex even when the
    // shader consumes only XY or RGB. Resolve AUTO from the actual upload
    // before both layout validation and PSO creation.
    if(!pass.original_dynamic_font_vertex_layout_verified && !pass.original_runtime_draw_verified) {
      for(auto& element:pass.exact_host_input_layout) {
        if(element.Stride==Diligent::LAYOUT_ELEMENT_AUTO_STRIDE)
          element.Stride=element.BufferSlot==0 ? sizeof(model::NativePosition) : sizeof(model::HostAttribute);
      }
    }
    if(frame.widescreen_camera) {
      // A verified original main perspective may expand into the full
      // native viewport. The original screen-space HUD must remain at its
      // own safe 16:9 aspect, and unverified offscreen/tiling passes may NOT
      // be stretched to the host window just because the output is ultrawide.
      if(!pass.original_view_space_verified ||
         pass.original_view_space==VerifiedOriginalViewSpace::Unknown ||
         (pass.original_view_space==VerifiedOriginalViewSpace::Offscreen && !formats.original_graph.active()) ||
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
      if(texture.identity.original_runtime_material!=pass.original_runtime_draw_verified ||
         texture.identity.original_model_hash!=owner.original_model_hash ||
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
      const auto reflected=aot::InspectSpirvImageResources(binary->spirv,stage);
      for(const auto& image:reflected) {
        if((image.spirv_dim!=0 && image.spirv_dim!=1 && image.spirv_dim!=3) ||
           image.image_multisampled || (image.spirv_dim!=1 && image.image_arrayed))
          throw std::invalid_argument("Unsupported original EoR image dimension");
        const auto view=image.spirv_dim==3 ? OriginalTextureViewKind::NativeCube :
            image.spirv_dim==0 ? OriginalTextureViewKind::Native1D :
            image.image_arrayed ? OriginalTextureViewKind::NativeX2DArray :
                                  OriginalTextureViewKind::Native2D;
        unsigned matches=0;
        for(const auto& texture:owner.original_textures)
          if(texture.identity.stage==
                (stage==aot::Stage::Vertex ? OriginalSamplerStage::Vertex :
                                             OriginalSamplerStage::Pixel) &&
             texture.identity.exact_shader_variable_name==image.exact_variable_name) {
            const auto fixed=aot::DecodeFixedAotImageSlot(image.exact_variable_name);
            if((fixed && fixed->slot!=texture.identity.original_fetch_register) ||
               texture.required_view!=view ||
               (texture.identity.original_resolved_guest_object ?
                 (view!=OriginalTextureViewKind::Native2D ||
                  stage!=aot::Stage::Pixel ||
                  !formats.original_graph.HasResolve(texture.identity.original_resolved_guest_object)) :
                 (!texture.uploaded || texture.uploaded->view_kind!=view)))
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
       gpu.DSVFormat!=(pass.original_depth_unbound ? Diligent::TEX_FORMAT_UNKNOWN : depth) ||
       (!pass.original_depth_unbound && formats.depth==NativeDepthFormat::None) ||
       gpu.SmplDesc.Count!=samples)
      throw std::invalid_argument("Native Vulkan PSO format/sample contract disagrees with scene");
    if(pass.original_dynamic_font_vertex_layout_verified) {
      const auto& layout=pass.exact_host_input_layout;
      std::vector<uint64_t> symbolic(layout.size(),1u);
      if(pass.original_view_space!=VerifiedOriginalViewSpace::OriginalAspectHud ||
         !pass.original_view_space_verified ||
         !pass.original_hud_clip_coordinates_verified ||
         !SourceFontSlotMask(layout,symbolic))
        throw std::invalid_argument(
            "Original font pass lacks proven HUD float4 input and clip semantics");
    } else if(pass.original_runtime_draw_verified) {
      std::vector<uint64_t> symbolic(pass.exact_host_input_layout.size(),1u);
      if(!SourceRuntimeSlotMask(pass.exact_host_input_layout,symbolic))
        throw std::invalid_argument("Runtime draw lacks verified separate float4/uint4 inputs");
    } else if(!HasVerifiedHostAttributeLayout(pass.exact_host_input_layout)) {
      throw std::invalid_argument(
          "EoR original vertex layout overruns or aliases actual host GPU attribute buffers");
    }
    if(!effects.HasPair(pass.original_effect_hash,pass.original_pass_index,
                        pass.original_vertex_shader_sha256,
                        pass.original_pixel_shader_sha256))
      throw std::invalid_argument("Native EoR shader/pass provenance mismatched");
    // State::material_lookup already rejects duplicate material and attachment variants
    // in O(log N) per pass, before any Vulkan pipeline is allocated.
}

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
     effects.ShaderCount()!=bank.expected())
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
  if(color==Diligent::TEX_FORMAT_UNKNOWN || (formats.depth==NativeDepthFormat::None ?
      (!formats.original_screen.exact_guest_to_native_targets_verified || !formats.original_screen.guest_device ||
       !formats.original_screen.guest_rt0 || formats.original_screen.guest_depth) :
      depth==Diligent::TEX_FORMAT_UNKNOWN))
    throw std::invalid_argument("Unknown EoR native scene format");
  for(auto& owner:state_->passes)
    ValidateSourceOriginalMaterialPass(owner,bank,effects,frame,formats);
  for(const auto& [position,resident]:state_->draw_buffers) {
    const auto& slots=resident.host_vertex_slots;
    if(!position || resident.position_handle!=position ||
       !resident.original_model_hash ||
       resident.original_dynamic_font_verified || resident.original_runtime_draw_verified ||
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
      if(!slots[i] || (state_->meshes.KindOf(slots[i])!=3 &&
                       state_->meshes.KindOf(slots[i])!=4))
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
std::size_t DiligentEorDirectBackend::AdoptVerifiedOriginalMaterialPasses(
    std::span<const VerifiedNativeMaterialPass> original_passes,
    DiligentOriginalTextureResidency& residency) {
  if(!state_ || state_->scene_open || state_->frame_preflight_ready)
    throw std::invalid_argument(
        "Source EoR material cannot change during Vulkan GPU scene");
  if(original_passes.empty())return 0;
  auto incoming=AcquireVerifiedNativeMaterialPasses(original_passes,residency);
  auto& s=*state_;
  // Every new material passes the same strict shader image/PSO checks
  // that the constructor requires of its initial original-source materials.
  for(auto& pass:incoming)
    ValidateSourceOriginalMaterialPass(pass,s.bank,s.effects,s.frame,s.format);
  for(const auto& pass:incoming) {
    const auto key=PassKey(pass);
    const auto previous=s.material_lookup.find(key);
    if(previous==s.material_lookup.end())continue;
    const auto& old=*previous->second;
    if(old.pipeline.original_vertex_shader_sha256!=
           pass.pipeline.original_vertex_shader_sha256 ||
       old.pipeline.original_pixel_shader_sha256!=
           pass.pipeline.original_pixel_shader_sha256 ||
       old.pipeline.original_xbox_cube_gamma_mask!=pass.pipeline.original_xbox_cube_gamma_mask ||
       old.original_textures.size()!=pass.original_textures.size() ||
       old.pipeline.exact_host_input_layout.size()!=pass.pipeline.exact_host_input_layout.size() ||
       old.pipeline.original_view_space!=pass.pipeline.original_view_space ||
       bool(old.pipeline.source_main_view)!=bool(pass.pipeline.source_main_view) ||
       (old.pipeline.source_main_view && pass.pipeline.source_main_view &&
        (old.pipeline.source_main_view->original_view_projection_first_register!=
             pass.pipeline.source_main_view->original_view_projection_first_register ||
         old.pipeline.source_main_view->source_proof.convention!=
             pass.pipeline.source_main_view->source_proof.convention)))
      throw std::invalid_argument(
          "Original material shader/image generation changed while resident");
    // A surface may reference more than one mesh. A different vertex
    // layout must not silently reuse the first mesh's cached Vulkan PSO.
    for(size_t i=0;i<pass.pipeline.exact_host_input_layout.size();++i) {
      const auto& x=old.pipeline.exact_host_input_layout[i];
      const auto& y=pass.pipeline.exact_host_input_layout[i];
      if(x.InputIndex!=y.InputIndex || x.BufferSlot!=y.BufferSlot ||
         x.NumComponents!=y.NumComponents || x.ValueType!=y.ValueType ||
         x.IsNormalized!=y.IsNormalized || x.RelativeOffset!=y.RelativeOffset ||
         x.Stride!=y.Stride || x.Frequency!=y.Frequency ||
         x.InstanceDataStepRate!=y.InstanceDataStepRate)
        throw std::invalid_argument("Original material vertex layout generation changed");
    }
    for(size_t i=0;i<pass.original_textures.size();++i) {
      const auto& a=old.original_textures[i];
      const auto& b=pass.original_textures[i];
      if(!Same(a.identity,b.identity) ||
         a.required_view!=b.required_view ||
         a.uploaded!=b.uploaded)
        throw std::invalid_argument(
            "Original material physical texture generation changed");
    }
  }
  size_t added=0;
  for(auto& pass:incoming) {
    const auto key=PassKey(pass);
    if(s.material_lookup.contains(key))continue;
    auto resident=std::make_unique<NativeEorPassResources>(std::move(pass));
    const auto* verified=resident.get();
    s.streamed_passes.emplace(key,std::move(resident));
    s.material_lookup.emplace(key,verified);
    ++added;
  }
  // Per-Draw adoption may run thousands of times in the first complete
  // native frame. Preflight creates exactly its needed PSOs/SRBs after
  // all source Draws validate; never repeatedly prewarm here. Initial
  // catalogue and resident mesh admission still have bounded prewarm.
  return added;
}
model::NativeMeshUploadBackend&
DiligentEorDirectBackend::OriginalModelUploader() noexcept {
  return state_->meshes;
}
std::size_t DiligentEorDirectBackend::AdoptVerifiedResidentWorldMeshes(
    std::span<const model::UploadedOriginalModel* const> residents) {
  if(!state_ || state_->scene_open || state_->frame_preflight_ready)
    throw std::invalid_argument(
        "Original EoR world meshes cannot change inside a GPU scene");
  if(residents.empty())return 0;
  auto& s=*state_;
  // The source archive GPU uploader has already authenticated the real
  // MODELS.ARC mesh headers and native-decoded position/index/UV streams.
  // This attaches those SAME GPU allocations to the live Draw dispatcher.
  auto incoming=PositionOnlyDrawBuffersFromOriginalModels(residents);
  for(const auto& draw:incoming) {
    if(!draw.position_handle || !draw.index_handle ||
       draw.vertex_count<3 || draw.index_count<3 ||
       !draw.original_model_hash ||
       draw.original_mesh_header_offset==SIZE_MAX ||
       s.meshes.KindOf(draw.position_handle)!=0u ||
       s.meshes.KindOf(draw.index_handle)!=2u)
      throw std::invalid_argument("Streamed original EoR mesh has no GPU owner");
    auto* vertex=s.meshes.FindBuffer(draw.position_handle);
    auto* index=s.meshes.FindBuffer(draw.index_handle);
    if(!vertex || !index ||
       vertex->GetDesc().Size <
           uint64_t(draw.vertex_count)*sizeof(model::NativePosition) ||
       index->GetDesc().Size <
           uint64_t(draw.index_count)*sizeof(uint32_t))
      throw std::invalid_argument("Original EoR streamed mesh allocation missing");
    // The complete index buffer may contain distinct draw windows with
    // different signed base vertices. Verify only their allocated bytes
    // here; actual index extrema are checked for EACH Draw in preflight.
    if(draw.host_vertex_slots.empty() ||
       draw.host_vertex_slots[0]!=draw.position_handle)
      throw std::invalid_argument("Original EoR vertex position slot missing");
    for(size_t slot=1;slot<draw.host_vertex_slots.size();++slot) {
      const auto handle=draw.host_vertex_slots[slot];
      auto* buffer=s.meshes.FindBuffer(handle);
      if(!buffer || (s.meshes.KindOf(handle)!=3u && s.meshes.KindOf(handle)!=4u) ||
         buffer->GetDesc().Size <
             uint64_t(draw.vertex_count)*sizeof(model::HostAttribute))
        throw std::invalid_argument("Original EoR decoded GPU attribute missing");
    }
    const auto old=s.draw_buffers.find(draw.position_handle);
    if(old!=s.draw_buffers.end()) {
      const auto& existing=old->second;
      if(existing.original_dynamic_font_verified || existing.original_runtime_draw_verified ||
         existing.original_model_hash!=draw.original_model_hash ||
         existing.original_mesh_header_offset!=draw.original_mesh_header_offset ||
         existing.index_handle!=draw.index_handle ||
         existing.vertex_count!=draw.vertex_count ||
         existing.index_count!=draw.index_count ||
         existing.host_vertex_slots!=draw.host_vertex_slots)
        throw std::invalid_argument(
            "Original EoR streamed GPU mesh identity unexpectedly changed");
    }
  }
  size_t inserted=0;
  for(auto& draw:incoming)
    if(s.draw_buffers.emplace(draw.position_handle,std::move(draw)).second)
      ++inserted;
  // Shader modules are compiled only for resident model passes, in a small
  // batch. Exact real Draw PSOs remain subject to normal source preflight.
  if(inserted)(void)PrewarmResidentOriginalPipelines(4);
  return inserted;
}
bool DiligentEorDirectBackend::CanReplayOriginalXdkCommand(
    const xdk::RawCommand& cmd) const {
  if(!state_)return false;
  const auto& s=*state_;
  const auto& original=s.format.original_screen;
  if(!original.exact_guest_to_native_targets_verified ||
     !original.guest_device || !original.guest_rt0 ||
     (!original.guest_depth && s.format.depth!=NativeDepthFormat::None) || !original.guest_front_buffer ||
     cmd.gpr[0]!=original.guest_device)
    return false;
  switch(cmd.kind) {
    case xdk::CommandKind::SetRenderTarget:
      return cmd.gpr[1]==0 && (s.format.original_graph.active() ?
          s.format.original_graph.HasColor(cmd.gpr[2]) : cmd.gpr[2]==original.guest_rt0);
    case xdk::CommandKind::SetDepthStencilSurface:
      return s.format.original_graph.active() ?
          s.format.original_graph.HasDepth(cmd.gpr[1]) :
          (!cmd.gpr[1] || cmd.gpr[1]==original.guest_depth);
    case xdk::CommandKind::ClearF:
      // This native render pass owns RT0 and one depth target only.
      // Xbox ClearF can address RT1..RT3, which we must not silently
      // ignore and then present as a fully reproduced source frame.
      return cmd.captured_clear &&
          (cmd.captured_clear->flags & 0x0eu)==0 &&
          (s.format.original_graph.active() ?
            std::any_of(s.format.original_graph.colors.begin(),s.format.original_graph.colors.end(),
              [&](uint32_t id){const auto size=s.format.original_graph.SourceExtent(id,s.frame.original_guest);
                return xdk::IsOriginalFullTargetClear(*cmd.captured_clear,size.width,size.height);}) :
            xdk::IsOriginalFullTargetClear(*cmd.captured_clear,s.frame.original_guest.width,
                                                          s.frame.original_guest.height)) &&
          (!(cmd.captured_clear->flags&0x30u) || s.format.depth!=NativeDepthFormat::None) &&
          ((cmd.captured_clear->flags & 0x20u)==0 ||
           s.format.depth==NativeDepthFormat::D24S8);
    case xdk::CommandKind::Resolve:
      if(s.format.original_graph.active())
        return !cmd.gpr[1] && !cmd.gpr[5] && !cmd.gpr[6] &&
           ((!cmd.gpr[2] && !cmd.gpr[4]) || cmd.captured_resolve) &&
           (cmd.gpr[3]==original.guest_front_buffer || s.format.original_graph.HasResolve(cmd.gpr[3]));
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
bool DiligentEorDirectBackend::RebindOriginalFrontBuffer(const VerifiedSceneFormat& verified) {
  if(!state_ || state_->scene_open || state_->frame_preflight_ready)return false;
  auto& s=*state_;
  const auto& a=s.format.original_screen;
  const auto& b=verified.original_screen;
  if(!s.format.original_graph.active() || !verified.original_graph.active() ||
     s.format.color!=verified.color || s.format.depth!=verified.depth ||
     !verified.original_color_format_verified || !verified.original_depth_format_verified ||
     !b.exact_guest_to_native_targets_verified || !b.final_resolve_to_front_buffer_verified ||
     b.guest_front_buffer!=verified.original_graph.front_buffer ||
     a.guest_device!=b.guest_device || a.guest_rt0!=b.guest_rt0 ||
     a.guest_depth!=b.guest_depth ||
     !SameOriginalTargetStorage(s.format.original_graph,verified.original_graph))return false;
  if(a.guest_front_buffer==b.guest_front_buffer)return true;
  // A failed queue fence may leave old staged references alive. Do not
  // mutate the owner of an epoch whose GPU lifetime has not been retired.
  if(!s.runtime_uploads.empty() || !s.font_uploads.empty())return false;
  auto next=verified;
  s.format=std::move(next);
  return true;
}
bool DiligentEorDirectBackend::CanReplayOriginalXdkTimeline(
    const xdk::Frame& source) const {
  if(!state_ || !xdk::ValidateOriginalCommandTimeline(source))return false;
  if(state_->format.original_graph.active()) {
    const auto plan=PlanOriginalNativeTargets(source,state_->frame.original_guest.width,
        state_->frame.original_guest.height);
    if(!plan || plan->samples!=state_->frame.samples ||
       plan->format.color!=state_->format.color || plan->format.depth!=state_->format.depth ||
       plan->format.original_graph!=state_->format.original_graph ||
       plan->format.original_screen.guest_device!=state_->format.original_screen.guest_device)
      return false;
    // Physical copy mappings must also be proven before any GPU Draw.
    const auto& graph=plan->format.original_graph;
    uint32_t active=graph.first_color;
    for(const auto& cmd:source.commands) {
      if(cmd.kind==xdk::CommandKind::SetRenderTarget)active=cmd.gpr[2];
      if(cmd.kind!=xdk::CommandKind::Resolve)continue;
      const auto a=graph.SourceExtent(active,state_->frame.original_guest);
      const auto d=graph.SourceExtent(cmd.gpr[3],state_->frame.original_guest);
      const auto ha=graph.HostExtent(active,state_->frame);
      const auto hd=graph.HostExtent(cmd.gpr[3],state_->frame);
      const auto region=xdk::DecodeOriginalColorResolveRegion(cmd,a.width,a.height);
      if(!region || !xdk::MapOriginalResolveRegion(*region,a.width,a.height,d.width,d.height,
                                  ha.width,ha.height,hd.width,hd.height))return false;
    }
    return true;
  }
  // This native EoR backend owns ONE verified main RT0/depth and ONE final
  // resolve. Other original passes (shadows, MRT, chained post-processing)
  // require distinct native target mappings; rejecting them is safer than
  // pretending a single host RT is equivalent to all guest EDRAM tiles.
  // D3D/XDK render-target binds persist across Present. The original
  // game may Clear and Draw without issuing SetRenderTarget/SetDepthStencil
  // again on *every* frame. Admit inherited bindings ONLY if the first
  // captured real Draw's device shadow independently proves the same
  // guest device, RT0, depth and shader/declaration owners as this host
  // target. A pre-Draw rebinding must establish its own source identity
  // before any Clear; no retroactive proof from a later Draw is allowed.
  const auto& first=source.calls.front();
  const bool inherited=first.observed_vs_matrices &&
      MatchesVerifiedOriginalXdkMainDrawTargets(
          *first.observed_vs_matrices,state_->format.original_screen,
          first.observed_vs_matrices->original_depth_guest_object!=0);
  bool pre_draw_rt_change=false,pre_draw_depth_change=false;
  for(const auto& event:source.commands) {
    if(event.draw_ordinal!=UINT32_MAX)break;
    if(event.kind==xdk::CommandKind::SetRenderTarget)
      pre_draw_rt_change=true;
    else if(event.kind==xdk::CommandKind::SetDepthStencilSurface)
      pre_draw_depth_change=true;
  }
  bool rt=inherited && !pre_draw_rt_change;
  std::optional<uint32_t> depth;
  if(inherited && !pre_draw_depth_change)depth=first.observed_vs_matrices->original_depth_guest_object;
  bool cleared=false,drew=false,resolved=false,tiling=false;
  for(const auto& cmd:source.commands) {
    if(cmd.draw_ordinal!=UINT32_MAX) {
      const auto& call=source.calls[cmd.draw_ordinal];
      if(!rt || !depth || !cleared || resolved || !call.observed_vs_matrices ||
         call.observed_vs_matrices->original_depth_guest_object!=*depth ||
         !MatchesVerifiedOriginalXdkMainDrawTargets(*call.observed_vs_matrices,
             state_->format.original_screen,*depth!=0))return false;
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
        depth=cmd.gpr[1];break;
      case xdk::CommandKind::ClearF:
        if(resolved || ((cmd.captured_clear->flags&1u) && !rt) ||
           ((cmd.captured_clear->flags&0x30u) && (!depth || !*depth)))
          return false;
        // A stencil/depth-only source Clear does not initialize this
        // independent host color target. Never present undefined pixels
        // under an otherwise valid original text/UI Draw.
        if(cmd.captured_clear->flags&1u)cleared=true;
        break;
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
    case xdk::CommandKind::SetDepthStencilSurface:
      return s.format.original_graph.active() ? s.scene.SetOriginalDepthTarget(cmd.gpr[1]) :
                                                s.scene.SetDepthBound(cmd.gpr[1]!=0);
    case xdk::CommandKind::SetRenderTarget:
      if(s.format.original_graph.active())return s.scene.SetOriginalColorTarget(cmd.gpr[2]);
      return true;
    case xdk::CommandKind::BeginTiling:
    case xdk::CommandKind::EndTiling:
      // Host scene is already bound. The exact source target identity and
      // (when tiled) alias-free mapping were proved in frame preflight.
      return true;
    case xdk::CommandKind::ClearF:
      if(!cmd.captured_clear)return false;
      if(cmd.captured_clear->flags&1u)s.resolve_reuse.ColorWritten(s.scene.ActiveOriginalColor());
      return s.scene.ClearVerifiedOriginalXdk(*cmd.captured_clear,s.frame.original_guest);
    case xdk::CommandKind::Resolve:
      // The real native color+depth target remains resident through
      // DrawIndexed; physical MSAA resolve is performed by ResolveScene()
      // immediately after the final source Draw, not against guest EDRAM.
      if(s.source_resolve_recorded)return false;
      if(s.format.original_graph.active()) {
        const auto size=s.scene.ActiveSourceExtent(s.frame.original_guest);
        const auto region=xdk::DecodeOriginalColorResolveRegion(cmd,size.width,size.height);
        if(!region)return false;
        if(cmd.gpr[3]!=s.format.original_screen.guest_front_buffer) {
          const auto color=s.scene.ActiveOriginalColor(),dest=cmd.gpr[3];
          const bool full=region->IsFull(size.width,size.height);
          if(s.resolve_reuse.CanReuse(color,dest,full) &&
             s.scene.IntermediateColorSRV(dest))return true;
          if(!s.scene.ResolveOriginalIntermediate(dest,region))return false;
          s.resolve_reuse.Resolved(color,dest,full);
          return true;
        }
        if(!region->IsFull(size.width,size.height))return false;
      }
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
  std::size_t attempted=0;
  // Avoid a pathological startup when a large number of source passes
  // have no compiled SPIR-V variant yet. Skip an unsupported pass instead
  // of abandoning prewarming ALL the valid resident UI/world materials.
  const std::size_t attempt_budget=std::max(max_new,std::size_t{128});
  // Resume after the last inspected key, even when its pipeline failed.
  // Restarting at begin() starves every material behind 128 unsupported
  // resident passes. Store a key, not an iterator: streamed material
  // insertion and retirement may change this map between Presents.
  auto cursor=s.prewarm_cursor ? s.material_lookup.upper_bound(*s.prewarm_cursor)
                               : s.material_lookup.begin();
  for(std::size_t remaining=s.material_lookup.size();
      remaining && warmed<max_new && attempted<attempt_budget;--remaining) {
    if(cursor==s.material_lookup.end())cursor=s.material_lookup.begin();
    const auto& [key,owner_ptr]=*cursor++;
    s.prewarm_cursor=key;
    const auto& owner=*owner_ptr;
    if(!resident_models.contains(owner.original_model_hash))continue;
    if(s.pipeline_cache.contains(key) &&
       s.material_bindings.contains(key))continue;
    ++attempted;
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
      // A missing/invalid source shader cannot poison preparation of the
      // NEXT independently verified material in this same resident scene.
      continue; // Prewarm is optional and must not break guest startup.
    }
  }
  return warmed;
}
aot::BoundIndexedDraw DiligentEorDirectBackend::StageVerifiedOriginalFontDraw(
    const xdk::RawDraw& call,
    const ui::VerifiedFontVertexLayout& layout,
    uint64_t frame,uint32_t ordinal,
    uint32_t original_font_resource_hash,uint32_t surface_ordinal,
    const aot::OriginalVirtualShaderIdentity& shader_identity) {
  if(!state_ || state_->scene_open || state_->frame_preflight_ready ||
     !frame || ordinal==UINT32_MAX ||
     !original_font_resource_hash || surface_ordinal==UINT32_MAX ||
     !call.observed_vs_matrices || !call.observed_shader_constants ||
     !call.observed_live_vertex_shader_header ||
     !call.observed_live_pixel_shader_header)
    throw std::invalid_argument("Original EFont draw has no complete live source");
  auto& s=*state_;
  const auto& live=*call.observed_vs_matrices;
  const auto original_vs=aot::ResolveOriginalLiveXdkShaderSource(
      shader_identity,*call.observed_live_vertex_shader_header,
      live.original_vs_guest_object,aot::Stage::Vertex,frame,ordinal);
  const auto original_ps=aot::ResolveOriginalLiveXdkShaderSource(
      shader_identity,*call.observed_live_pixel_shader_header,
      live.original_ps_guest_object,aot::Stage::Pixel,frame,ordinal);
  if(!original_vs || !original_ps)
    throw std::invalid_argument("Original EFont VS/PS guest header not authenticated");
  // This is actual transient EFontShader vertex/index data emitted by the
  // game, with an independently verified source XDK declaration and atlas.
  auto cpu=ui::DecodeOriginalLiveFontDraw(call,layout,frame,ordinal);
  const MaterialKey material{
      original_font_resource_hash,surface_ordinal,
      cpu.original_effect_hash,cpu.original_pass_index,!live.original_depth_guest_object,false,
      s.format.original_graph.active()?live.original_rt0_guest_object:0u};
  const auto found=s.material_lookup.find(material);
  if(found==s.material_lookup.end())
    throw std::invalid_argument("Original EFont archive material/pass not resident");
  const auto* owner=found->second;
  const auto& pass=owner->pipeline;
  if(!MatchesVerifiedOriginalXdkDrawTargets(live,s.format,!pass.original_depth_unbound))
    throw std::invalid_argument("Original EFont render target does not match its material pass");
  // Font_Main VS+PS were recovered from the original USA ShaderEffects
  // source and their exact 508/328-byte Xenos ShaderContainers. Match BOTH
  // SHA-256 identities from the CURRENT live guest XDK objects before a
  // transient glyph Draw can use a native Vulkan material PSO.
  if(!ui::MatchesOriginalUsaFontMainProgram(
         cpu.original_effect_hash,cpu.original_pass_index,
         *original_vs,*original_ps) ||
     !pass.original_dynamic_font_vertex_layout_verified ||
     !pass.original_view_space_verified ||
     pass.original_view_space!=VerifiedOriginalViewSpace::OriginalAspectHud ||
     !pass.original_hud_clip_coordinates_verified ||
     pass.original_vertex_shader_sha256!=*original_vs ||
     pass.original_pixel_shader_sha256!=*original_ps ||
     !s.effects.HasPair(cpu.original_effect_hash,cpu.original_pass_index,
                        *original_vs,*original_ps) ||
     !s.bank.Find(*original_vs,aot::Stage::Vertex) ||
     !s.bank.Find(*original_ps,aot::Stage::Pixel))
    throw std::invalid_argument("Original EFont material/source shader pair disagrees");
  const auto& input=pass.exact_host_input_layout;
  const size_t slot_count=layout.color ? 3u:2u;
  if(input.size()!=slot_count)
    throw std::invalid_argument("Original EFont shader expects another vertex declaration");
  for(size_t i=0;i<input.size();++i) {
    const uint32_t components=i==0 ? layout.position.components :
        i==1 ? layout.texcoord.components : layout.color->components;
    constexpr uint32_t font_input_locations[]={0u,4u,8u}; // position, UV0, color
    if(input[i].InputIndex!=font_input_locations[i] ||
       input[i].BufferSlot!=i || input[i].NumComponents!=components ||
       input[i].ValueType!=Diligent::VT_FLOAT32 ||
       (input[i].RelativeOffset!=0 &&
        input[i].RelativeOffset!=Diligent::LAYOUT_ELEMENT_AUTO_OFFSET) ||
       (input[i].Stride!=16u &&
        input[i].Stride!=Diligent::LAYOUT_ELEMENT_AUTO_STRIDE))
      throw std::invalid_argument("Original EFont host input differs from decoded XDK declaration");
  }
  // A font atlas is a real Textures.ARC key owned by THIS material pass.
  // A guessed glyph image, nearby UI logo or zero-sampler shader cannot
  // authorize native Vulkan presentation.
  unsigned atlases=0;
  for(const auto& image:owner->original_textures) {
    const auto& id=image.identity;
    if(id.stage==OriginalSamplerStage::Pixel &&
       id.original_texture_hash==cpu.original_font_atlas_hash &&
       id.original_material_ownership_verified &&
       id.original_shader_fetch_slot_verified &&
       id.original_texture_format_and_mips_verified &&
       image.uploaded && image.uploaded->SRV())
      ++atlases;
  }
  if(atlases!=1u || owner->original_textures.size()!=1u || !call.original_font_fetch ||
     owner->original_textures[0].identity.original_fetch_register!=0 ||
     owner->original_textures[0].identity.exact_shader_variable_name!="HulkTexture2D_s0")
    throw std::invalid_argument("Original EFont pixel atlas does not belong to live material");
  const State::DrawKey draw_key{frame,ordinal};
  if(s.font_uploads.contains(draw_key))
    throw std::invalid_argument("Original EFont frame/ordinal already uploaded");
  // All source identity checks above precede the FIRST Vulkan allocation.
  auto pipeline=s.pipeline_cache.find(material);
  if(pipeline==s.pipeline_cache.end()) {
    auto pso=CreateOriginalVulkanPipeline(s.device,s.shaders,s.bank,s.effects,pass);
    pipeline=s.pipeline_cache.emplace(material,std::move(pso)).first;
  }
  // The atlas can be shared by text in different original depth/blend/
  // color-mask states. Bind this glyph's SRB to the exact live PSO, not
  // the first glyph's cached material state.
  const auto& fixed=call.observed_shader_constants->fixed_graphics_state;
  if(!fixed || fixed->original_guest_device!=live.original_guest_device)
    throw std::invalid_argument("Original EFont draw lacks fixed graphics state");
  auto* font_pipeline=pipeline->second.pipeline.RawPtr();
  if(!MatchesOriginalXdkGraphicsState(pass,*fixed)) {
    const auto derived=DeriveOriginalXdkGraphicsPipelineState(*fixed,pass.exact_render_state);
    const auto variant=OriginalPipelineVariantKey(material,*fixed);
    if(!derived || !variant ||
       derived->SmplDesc.Count!=s.format.original_graph.Samples(live.original_rt0_guest_object,s.frame.samples))
      throw std::invalid_argument("Original EFont PSO variant changes target contract");
    auto exact=pass;
    exact.exact_render_state=*derived;
    if(!MatchesOriginalXdkGraphicsState(exact,*fixed))
      throw std::invalid_argument("Original EFont PSO variant does not match live state");
    auto found_variant=s.pipeline_variants.find(*variant);
    if(found_variant==s.pipeline_variants.end()) {
      auto pso=CreateOriginalVulkanPipeline(s.device,s.shaders,s.bank,s.effects,exact);
      found_variant=s.pipeline_variants.emplace(*variant,std::move(pso)).first;
    }
    font_pipeline=found_variant->second.pipeline.RawPtr();
  }
  // Distinct glyph batches can share an atlas while changing its sampling
  // mode. Keep the descriptor with this draw until its GPU fence completes.
  if(!font_pipeline)throw std::runtime_error("Original EFont PSO unavailable");
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> font_bindings;
  font_pipeline->CreateShaderResourceBinding(&font_bindings,true);
  BindUsaFontMainAtlas(s.device,font_bindings,*owner->original_textures[0].uploaded,
                       *call.original_font_fetch);
  auto gpu=ui::UploadOriginalLiveFontDraw(cpu,s.meshes);
  const auto handle=gpu.position_handle;
  NativeEorDrawBuffers buffers{};
  buffers.position_handle=handle;
  buffers.index_handle=gpu.index_handle;
  buffers.vertex_count=gpu.vertex_count;
  buffers.index_count=gpu.index_count;
  buffers.host_vertex_slots={gpu.position_handle,gpu.texcoord_handle};
  if(gpu.color_handle)
    buffers.host_vertex_slots.push_back(gpu.color_handle);
  buffers.original_model_hash=original_font_resource_hash;
  buffers.original_mesh_header_offset=SIZE_MAX; // transient, NOT MODELS.ARC
  buffers.original_dynamic_font_verified=true;
  buffers.original_font_frame=frame;
  buffers.original_font_draw_ordinal=ordinal;
  buffers.original_font_atlas_hash=cpu.original_font_atlas_hash;
  buffers.font_bindings=std::move(font_bindings);
  auto [storage,added]=s.font_uploads.emplace(draw_key,std::move(gpu));
  if(!added)throw std::logic_error("Duplicate EFont source GPU generation");
  try {
    if(!s.draw_buffers.emplace(handle,std::move(buffers)).second)
      throw std::logic_error("Duplicate live EFont host buffer identity");
  } catch (...) {
    s.font_uploads.erase(storage);
    throw;
  }
  aot::BoundIndexedDraw result{};
  result.draw.vertex_buffer=handle;
  result.draw.index_buffer=storage->second.index_handle;
  result.draw.vertex_shader=live.original_vs_guest_object;
  result.draw.pixel_shader=live.original_ps_guest_object;
  result.draw.index_count=storage->second.index_count;
  result.draw.first_index=0;
  result.draw.base_vertex=0;
  result.draw.source_frame=frame;
  result.draw.source_ordinal=ordinal;
  result.draw.guest_vs_matrices=*call.observed_vs_matrices;
  result.draw.guest_shader_constants=call.observed_shader_constants;
  result.vertex_source_sha256=*original_vs;
  result.pixel_source_sha256=*original_ps;
  result.effect_hash=cpu.original_effect_hash;
  result.pass_index=cpu.original_pass_index;
  result.original_model_hash=original_font_resource_hash;
  result.original_surface_ordinal=surface_ordinal;
  result.original_mesh_header_offset=SIZE_MAX;
  result.source_nonindexed_linearization_verified=
      bool(xdk::DecodeOriginalFontVertices(call));
  result.original_dynamic_font_draw=true;
  for(const auto& image:owner->original_textures)
    result.original_samplers.push_back(image.identity);
  return result;
}
aot::BoundIndexedDraw DiligentEorDirectBackend::StageOriginalRuntimeDraw(
    const xdk::RawDraw& call,uint64_t frame,uint32_t ordinal,
    const VerifiedNativeMaterialPass& material,const DecodedRuntimeGeometry& cpu,
    DiligentOriginalTextureResidency& textures) {
  if(!state_ || state_->scene_open || state_->frame_preflight_ready ||
     !frame || ordinal==UINT32_MAX || !call.observed_vs_matrices ||
     !call.observed_shader_constants ||
     !call.observed_live_eor_effect_pass || !material.original_pipeline.original_runtime_draw_verified ||
     material.original_surface_ordinal!=ordinal || cpu.layout.size()!=cpu.attributes.size() ||
     cpu.layout.size()!=cpu.integer_attributes.size() || !cpu.vertex_count || cpu.indices.empty() ||
     cpu.indices.size()>UINT32_MAX || (!cpu.point_list && cpu.indices.size()%3) ||
     cpu.point_list!=xdk::IsOriginalRuntimePointDraw(call) ||
     cpu.point_list!=(material.original_pipeline.exact_render_state.PrimitiveTopology==
                      Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST))
    throw std::invalid_argument("Incomplete original transient Draw owner");
  auto& s=*state_;
  const auto& live=*call.observed_vs_matrices;
  const auto& pass=material.original_pipeline;
  if(pass.original_direct_xdk_runtime) {
    std::vector<VerifiedEorSampler> bindings;
    for(const auto& image:material.original_images) {
      if(image.original_binding.original_resolved_guest_object &&
         !s.format.original_graph.HasResolve(image.original_binding.original_resolved_guest_object))
        throw std::invalid_argument("Direct XDK texture is not a resident source Resolve");
      bindings.push_back(image.original_binding);
    }
    if(!aot::ProvesDirectXdkRuntimeBindings(call,bindings,material.original_model_hash))
      throw std::invalid_argument("Direct XDK material owner mismatch");
  } else if(!call.original_engine_shader_method ||
            !call.original_engine_shader_method->original_pass_vs_ps_xdk_binding_verified ||
            material.original_model_hash!=call.original_engine_shader_method->original_pass_material_context_guest)
    throw std::invalid_argument("Transient original material context mismatch");
  if(pass.original_effect_hash!=call.observed_live_eor_effect_pass->effect_hash ||
     pass.original_pass_index!=call.observed_live_eor_effect_pass->pass_index ||
     !s.effects.HasPair(pass.original_effect_hash,pass.original_pass_index,
                        pass.original_vertex_shader_sha256,pass.original_pixel_shader_sha256) ||
     !MatchesVerifiedOriginalXdkDrawTargets(live,s.format,!pass.original_depth_unbound) ||
     pass.exact_host_input_layout.size()!=cpu.layout.size() ||
     !std::all_of(cpu.indices.begin(),cpu.indices.end(),[&](uint32_t i){return i<cpu.vertex_count;}))
    throw std::invalid_argument("Transient Draw source/PSO mismatch");
  const auto key=PassKey(material.original_model_hash,ordinal,pass);
  const State::DrawKey epoch{frame,ordinal};
  if(s.runtime_uploads.contains(epoch) || s.material_lookup.contains(key))
    throw std::invalid_argument("Transient Draw epoch already staged");
  auto owner=std::make_unique<State::RuntimeUpload>();
  owner->gpu=&s.meshes;owner->material=key;
  owner->handles.reserve(cpu.layout.size()+1);
  auto add=[&](uint64_t handle) {
    if(!handle)throw std::runtime_error("Transient native Vulkan buffer allocation failed");
    owner->handles.push_back(handle);return handle;
  };
  NativeEorDrawBuffers buffers{};
  buffers.host_vertex_slots.resize(cpu.layout.size());
  for(size_t i=0;i<cpu.layout.size();++i) {
    const auto& e=cpu.layout[i];const auto& p=pass.exact_host_input_layout[i];
    if(e.InputIndex!=p.InputIndex || e.BufferSlot!=p.BufferSlot ||
       e.ValueType!=p.ValueType || e.NumComponents!=p.NumComponents ||
       e.Stride!=p.Stride || e.RelativeOffset!=p.RelativeOffset ||
       e.BufferSlot!=i)throw std::invalid_argument("Transient vertex layout changed");
    const auto storage=OriginalVertexStorageKind(e.BufferSlot,e.InputIndex,
        e.ValueType==Diligent::VT_UINT32,true,false);
    if(storage!=3u && storage!=4u)
      throw std::invalid_argument("Transient vertex storage ABI unsupported");
    if(storage==4u) {
      if(cpu.integer_attributes[i].size()!=cpu.vertex_count || !cpu.attributes[i].empty())
        throw std::invalid_argument("Transient integer input incomplete");
      buffers.host_vertex_slots[i]=add(s.meshes.UploadDecodedIntegerAttributes(cpu.integer_attributes[i]));
    } else {
      if(cpu.attributes[i].size()!=cpu.vertex_count || !cpu.integer_attributes[i].empty())
        throw std::invalid_argument("Transient float input incomplete");
      buffers.host_vertex_slots[i]=add(s.meshes.UploadDecodedAttributes(cpu.attributes[i]));
    }
  }
  if(!SourceRuntimeSlotMask(cpu.layout,buffers.host_vertex_slots))
    throw std::invalid_argument("Transient native vertex slot ABI invalid");
  buffers.position_handle=owner->position=buffers.host_vertex_slots.front();
  buffers.index_handle=add(cpu.point_list?s.meshes.UploadPointIndices(cpu.indices):s.meshes.UploadIndices(cpu.indices));
  buffers.vertex_count=cpu.vertex_count;buffers.index_count=static_cast<uint32_t>(cpu.indices.size());
  buffers.original_model_hash=material.original_model_hash;
  buffers.original_mesh_header_offset=SIZE_MAX;
  buffers.original_runtime_draw_verified=true;buffers.original_runtime_frame=frame;
  buffers.original_runtime_ordinal=ordinal;
  const auto position=buffers.position_handle,index=buffers.index_handle;
  const auto count=buffers.index_count;
  s.runtime_uploads.emplace(epoch,std::move(owner));
  try {
    AdoptVerifiedOriginalMaterialPasses({&material,1},textures);
    if(!s.draw_buffers.emplace(position,std::move(buffers)).second)
      throw std::logic_error("Transient native vertex handle collision");
    aot::BoundIndexedDraw result{};
    result.draw={position,index,live.original_vs_guest_object,live.original_ps_guest_object,
      count,0,0,frame,ordinal,live,call.observed_shader_constants};
    result.vertex_source_sha256=pass.original_vertex_shader_sha256;
    result.pixel_source_sha256=pass.original_pixel_shader_sha256;
    result.effect_hash=pass.original_effect_hash;result.pass_index=pass.original_pass_index;
    result.original_model_hash=material.original_model_hash;result.original_surface_ordinal=ordinal;
    result.original_mesh_header_offset=SIZE_MAX;result.original_runtime_draw=true;
    result.original_point_list=cpu.point_list;
    result.original_direct_xdk_runtime=pass.original_direct_xdk_runtime;
    result.source_nonindexed_linearization_verified=bool(xdk::DecodeOriginalVerticesXdkCall(call));
    if(call.observed_live_pixel_shader_header)
      result.original_pixel_sampling=call.observed_live_pixel_shader_header->pixel_sampling;
    for(const auto& image:material.original_images)result.original_samplers.push_back(image.original_binding);
    return result;
  } catch (...) {ReleaseRuntimeDraws(frame,false);throw;}
}
void DiligentEorDirectBackend::ReleaseRuntimeDraws(uint64_t frame,bool completed) noexcept {
  if(!state_ || state_->scene_open || !frame)return;
  auto& s=*state_;
  const auto first=completed ? s.runtime_uploads.begin() :
      s.runtime_uploads.lower_bound({frame,0u});
  for(auto it=first;it!=s.runtime_uploads.end() && it->first.first<=frame;) {
    const auto& owner=*it->second;
    s.draw_buffers.erase(owner.position);
    s.material_lookup.erase(owner.material);
    s.material_bindings.erase(owner.material);s.pipeline_cache.erase(owner.material);
    // Variant keys are ordered by (material, exact register tuple).
    // Retire only this owner's contiguous range, not the entire PSO map
    // once per transient world Draw.
    auto p=s.pipeline_variants.lower_bound({owner.material,{}});
    while(p!=s.pipeline_variants.end() && p->first.first==owner.material)
      p=s.pipeline_variants.erase(p);
    s.streamed_passes.erase(owner.material);
    it=s.runtime_uploads.erase(it);
  }
}
void DiligentEorDirectBackend::DiscardUnsubmittedOriginalFontDraws(
    uint64_t rejected_frame) noexcept {
  if(!state_ || state_->scene_open || !rejected_frame)return;
  ReleaseRuntimeDraws(rejected_frame,false);
  auto& s=*state_;
  for(auto it=s.font_uploads.lower_bound({rejected_frame,0u});
      it!=s.font_uploads.end() &&
      it->first.first==rejected_frame;) {
    s.draw_buffers.erase(it->second.position_handle);
    it=s.font_uploads.erase(it);
  }
}
void DiligentEorDirectBackend::RetireCompletedOriginalFontDraws(
    uint64_t completed_frame) noexcept {
  if(!state_ || state_->scene_open || !completed_frame)return;
  ReleaseRuntimeDraws(completed_frame,true);
  auto& s=*state_;
  // Caller must have observed the actual Vulkan completion fence, not
  // merely the next original guest Present or a CPU-side submission.
  for(auto it=s.font_uploads.begin();it!=s.font_uploads.end();) {
    if(it->first.first>completed_frame)break;
    s.draw_buffers.erase(it->second.position_handle);
    it=s.font_uploads.erase(it);
  }
}
bool DiligentEorDirectBackend::SetHostPresentationOptions(int32_t anisotropy,
    std::map<std::string,std::string,std::less<>> bindings) {
  if(!state_ || state_->scene_open || state_->frame_preflight_ready)return false;
  state_->world_anisotropy=anisotropy>=-1 && anisotropy<=5?anisotropy:-1;
  state_->pc_bindings=std::move(bindings);
  return true;
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
  std::set<MaterialKey> needed_materials;
  std::map<PipelineVariantKey,VerifiedOriginalGraphicsPass> needed_variants;
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
    if(!d.guest_vs_matrices)return false;
    const MaterialKey key{item.original_model_hash,item.original_surface_ordinal,
                          item.effect_hash,item.pass_index,!d.guest_vs_matrices->original_depth_guest_object,item.original_runtime_draw,
                          s.format.original_graph.active()?d.guest_vs_matrices->original_rt0_guest_object:0u};
    const auto owner=s.material_lookup.find(key);
    const NativeEorPassResources* selected=
        owner==s.material_lookup.end()?nullptr:owner->second;
    if(!selected ||
       selected->pipeline.original_vertex_shader_sha256!=item.vertex_source_sha256 ||
       selected->pipeline.original_pixel_shader_sha256!=item.pixel_source_sha256 ||
       selected->pipeline.original_direct_xdk_runtime!=item.original_direct_xdk_runtime ||
       selected->original_textures.size()!=item.original_samplers.size())
      return false;
    // The exact per-Draw XDK device shadow is independent of the
    // interleaved command log. Reject unknown/changed RT0 or depth owners
    // BEFORE pipeline compilation, color clears or the first Vulkan Draw.
    if(!d.source_frame ||
       !MatchesVerifiedOriginalXdkDrawTargets(
           *d.guest_vs_matrices,s.format,!selected->pipeline.original_depth_unbound) ||
       !d.guest_shader_constants)
      return false;
    // The native shader's reflected block selects direct c0..255 addressing
    // or the source translator's verified packed layout for each stage.
    try {
      State::PackedConstants packed;
      const auto target=d.guest_vs_matrices->original_rt0_guest_object;
      const auto source_extent=s.format.original_graph.SourceExtent(target,s.frame.original_guest);
      const auto host_extent=s.format.original_graph.active() ?
          s.format.original_graph.HostExtent(target,s.frame) : s.frame.world;
      const bool original_offscreen=source_extent.width!=s.frame.original_guest.width ||
                                    source_extent.height!=s.frame.original_guest.height;
      if(selected->pipeline.original_view_space==VerifiedOriginalViewSpace::Offscreen &&
         (!original_offscreen || source_extent.width!=host_extent.width || source_extent.height!=host_extent.height))
        return false;
      native::Rect viewport_area{0,0,host_extent.width,host_extent.height};
      if(s.frame.widescreen_camera &&
         selected->pipeline.original_view_space==VerifiedOriginalViewSpace::OriginalAspectHud) {
        const auto fitted=display::FitOriginalPicture(
            host_extent.width,host_extent.height,source_extent.width,source_extent.height);
        if(!fitted.valid)return false;
        viewport_area={fitted.x,fitted.y,fitted.width,fitted.height};
      }
      if(!d.guest_vs_matrices->raster)return false;
      const auto raster=native::MapOriginalRasterWindow(
          *d.guest_vs_matrices->raster,source_extent,viewport_area);
      if(!raster)return false;
      packed.raster=*raster;
      const auto& registers=*d.guest_shader_constants;
      const auto& pass=selected->pipeline;
      // Every actual Draw must still carry the genuine GPU fixed-function
      // generation used to produce its source-authenticated Vulkan PSO.
      // Reusing a material across cull/depth/blend transitions without this
      // check was able to render the correct mesh with the WRONG pipeline.
      if(!registers.fixed_graphics_state ||
         registers.fixed_graphics_state->original_guest_device!=
             d.guest_vs_matrices->original_guest_device)
        return false;
      const auto& live_state=*registers.fixed_graphics_state;
      if(!MatchesOriginalXdkGraphicsState(pass,live_state)) {
        // A streamed material owns the shader pair, vertex layout and
        // images, but fixed-function state belongs to the actual Draw.
        // Derive an additional PSO only from that immutable XDK snapshot.
        // EFont stages its own SRB against the same exact variant key.
        if(!pass.original_render_state_verified)return false;
        const auto derived=DeriveOriginalXdkGraphicsPipelineState(
            live_state,pass.exact_render_state);
        const auto variant=OriginalPipelineVariantKey(key,live_state);
        // Render-pass attachments and sample count are owned by the
        // established scene; a PSO variant must not change that contract.
        if(!derived || !variant ||
           derived->SmplDesc.Count!=s.format.original_graph.Samples(target,s.frame.samples))return false;
        auto verified_variant=pass;
        verified_variant.exact_render_state=*derived;
        if(!MatchesOriginalXdkGraphicsState(verified_variant,live_state))
          return false;
        packed.variant=*variant;
        if(!s.pipeline_variants.contains(*variant))
          needed_variants.try_emplace(*variant,std::move(verified_variant));
      }
      auto prepare=[&](aot::Stage stage,const aot::Digest& source,
                       const std::optional<aot::VerifiedOriginalFloatLayout>& layout,
                       std::vector<float>& values) {
        const auto abi=s.shaders.GetFloatRegisterAbi(s.bank,source,stage);
        if(abi==FloatRegisterAbi::External) {
          if(!layout)throw std::invalid_argument("Missing original shader register layout");
          values=aot::PackOriginalXdkFloatConstants(registers,*layout,stage,source);
        } else if(abi==FloatRegisterAbi::Direct256 && !registers.complete_float_banks) {
          throw std::invalid_argument("Missing complete original XDK float register shadow");
        }
        return abi;
      };
      packed.vs_abi=prepare(aot::Stage::Vertex,item.vertex_source_sha256,
                            pass.original_vs_float_layout,packed.vertex);
      packed.ps_abi=prepare(aot::Stage::Pixel,item.pixel_source_sha256,
                            pass.original_ps_float_layout,packed.pixel);
      if(s.frame.widescreen_camera &&
         pass.original_view_space==VerifiedOriginalViewSpace::MainPerspective && pass.source_main_view) {
        const auto& view=*pass.source_main_view;
        if(view.live_scalar_matrix_proof) {
          if(packed.vs_abi!=FloatRegisterAbi::Direct256)return false;
          const auto* vertex=s.bank.Find(item.vertex_source_sha256,aot::Stage::Vertex);
          if(!vertex || !item.draw.guest_shader_constants)return false;
          // The current reflection is independent of the material's
          // cached camera convention; require matching live matrix.
          aot::ReflectedShader reflected;
          reflected.stage=aot::Stage::Vertex;
          reflected.view_projection={view.original_view_projection_first_register,4u};
          packed.vertex=aot::AdaptLiveDirectScalarProjection(*vertex,reflected,
              registers,view.original_guest_aspect,s.frame.world.width,s.frame.world.height,
              view.original_view_projection_first_register,view.source_proof.convention);
        } else {
        if(packed.vs_abi==FloatRegisterAbi::Unused || !pass.original_vs_float_layout ||
           pass.original_vs_float_layout->original_shader_sha256!=item.vertex_source_sha256)return false;
        const bool direct=packed.vs_abi==FloatRegisterAbi::Direct256;
        const std::span<const float> original=direct ? std::span<const float>(registers.vertex)
                                                     : std::span<const float>(packed.vertex);
        packed.vertex=aot::ExpandVerifiedOriginalViewProjection(original,*pass.original_vs_float_layout,
            view.original_view_projection_first_register,view.original_guest_aspect,
            s.frame.world.width,s.frame.world.height,view.source_proof,
            direct ? aot::FloatRegisterOrder::Original : aot::FloatRegisterOrder::Packed);
        }
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
       mesh->second.index_handle!=d.index_buffer)
      return false;
    const bool is_font=mesh->second.original_dynamic_font_verified;
    const bool runtime=mesh->second.original_runtime_draw_verified;
    if(item.original_point_list!=(selected->pipeline.exact_render_state.PrimitiveTopology==
          Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST) ||
       (item.original_point_list && !runtime) ||
       runtime!=item.original_runtime_draw || runtime!=selected->pipeline.original_runtime_draw_verified ||
       (runtime && is_font) || item.original_dynamic_font_draw!=is_font ||
       selected->pipeline.original_dynamic_font_vertex_layout_verified!=is_font)
      return false;
    if(is_font) {
      // A transient font draw NEVER owns a MODELS.ARC mesh header. The
      // caller must stage exactly this original source frame and ordinal,
      // with the atlas hash validated against the selected material pass.
      if(item.original_mesh_header_offset!=SIZE_MAX ||
         mesh->second.original_mesh_header_offset!=SIZE_MAX ||
         mesh->second.original_font_frame!=d.source_frame ||
         mesh->second.original_font_draw_ordinal!=d.source_ordinal ||
         !mesh->second.original_font_atlas_hash || !mesh->second.font_bindings)
        return false;
    } else if(runtime) {
      if(item.original_mesh_header_offset!=SIZE_MAX || mesh->second.original_mesh_header_offset!=SIZE_MAX ||
         mesh->second.original_runtime_frame!=d.source_frame || mesh->second.original_runtime_ordinal!=d.source_ordinal)return false;
    } else if(item.original_mesh_header_offset==SIZE_MAX ||
              mesh->second.original_mesh_header_offset!=
                  item.original_mesh_header_offset)
      return false;
    if(!(is_font ?
         SourceFontSlotMask(
             selected->pipeline.exact_host_input_layout,
             mesh->second.host_vertex_slots) :
         runtime ? SourceRuntimeSlotMask(selected->pipeline.exact_host_input_layout,mesh->second.host_vertex_slots) :
         SourceVertexSlotMask(
             selected->pipeline.exact_host_input_layout,
             mesh->second.host_vertex_slots)) ||
       !s.meshes.IndexWindowInRange(d.index_buffer,d.first_index,
                                    d.index_count,d.base_vertex,
                                    mesh->second.vertex_count,item.original_point_list))
      return false;
    // Check typed slot storage before the first GPU clear, not only at Draw.
    for(const auto& element:selected->pipeline.exact_host_input_layout) {
      const auto slot=element.BufferSlot;
      const uint32_t kind=OriginalVertexStorageKind(slot,element.InputIndex,
          element.ValueType==Diligent::VT_UINT32,runtime,is_font);
      if(kind==UINT32_MAX || slot>=mesh->second.host_vertex_slots.size() ||
         s.meshes.KindOf(mesh->second.host_vertex_slots[slot])!=kind)return false;
    }
    needed_materials.insert(key);
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
    // Only now, after all original Draws pass source validation, compile
    // additional state variants. Their own SRB is required even when
    // the shader bytecode and original image ownership are identical.
    for(const auto& [key,pass]:needed_variants) {
      if(s.pipeline_variants.contains(key))continue;
      const auto owner=s.material_lookup.find(key.first);
      if(owner==s.material_lookup.end())return false;
      auto pipeline=CreateOriginalVulkanPipeline(
          s.device.RawPtr(),s.shaders,s.bank,s.effects,pass);
      BindVerifiedOriginalEorTextures(pipeline.bindings.RawPtr(),
                                     owner->second->original_textures);
      s.pipeline_variants.emplace(key,std::move(pipeline));
    }
    for(const auto& item:draws) {
      if(!item.draw.source_frame)continue;
      const MaterialKey key{item.original_model_hash,item.original_surface_ordinal,
                            item.effect_hash,item.pass_index,!item.draw.guest_vs_matrices->original_depth_guest_object,item.original_runtime_draw,
                            s.format.original_graph.active()?item.draw.guest_vs_matrices->original_rt0_guest_object:0u};
      const auto& textures=s.material_lookup.at(key)->original_textures;
      auto& prepared=prepared_constants.at({item.draw.source_frame,item.draw.source_ordinal});
      auto* pipeline=s.pipeline_cache.at(key).pipeline.RawPtr();
      if(prepared.variant) {
        const auto& variant=s.pipeline_variants.at(*prepared.variant);
        if(!variant.pipeline || !variant.bindings)return false;
        prepared.variant_pipeline=variant.pipeline;
        prepared.draw_bindings=variant.bindings;
        pipeline=variant.pipeline.RawPtr();
      }
      // EFont already owns a per-glyph atlas SRB created on its exact
      // base or variant PSO. Do not overwrite it with a shared descriptor.
      if(item.original_dynamic_font_draw)continue;
      const bool scalar=std::any_of(textures.begin(),textures.end(),[](const auto& texture) {
        return aot::DecodeFixedAotImageSlot(texture.identity.exact_shader_variable_name).has_value();
      });
      if(!scalar)continue;
      if(!item.original_pixel_sampling || item.original_pixel_sampling->source!=item.pixel_source_sha256)
        return false;
      const auto& controls=*item.original_pixel_sampling;
      const auto& captured=*item.draw.guest_shader_constants;
      auto& binding=prepared.draw_bindings;
      // A scalar AOT image needs its own live sampler state for this Draw.
      // Release any shared variant SRB before creating a per-Draw one.
      binding.Release();
      pipeline->CreateShaderResourceBinding(&binding,true);
      if(!binding)return false;
      uint16_t bound=0;
      for(const auto& texture:textures) {
        const auto& id=texture.identity;
        const auto image=aot::DecodeFixedAotImageSlot(id.exact_shader_variable_name);
        if(id.stage!=OriginalSamplerStage::Pixel || !image || image->dimension==2 || image->dimension>3 ||
           image->slot!=id.original_fetch_register)return false;
        const auto bit=static_cast<uint16_t>(1u<<image->slot);
        if(!(controls.sampled_slots&bit) || !(captured.valid_pixel_fetch&bit) || (bound&bit))
          return false;
        if(id.original_resolved_guest_object) {
          if(image->dimension!=1u || !s.format.original_graph.HasResolve(id.original_resolved_guest_object) ||
             item.draw.guest_vs_matrices->original_texture_guest_objects[image->slot]!=id.original_resolved_guest_object)
            return false;
          PrepareOriginalResolvedPixelSampler(s.device,binding,image->slot,captured.pixel_fetch[image->slot],
              (controls.point_slots&bit)!=0,
              s.format.original_graph.SourceExtent(id.original_resolved_guest_object,s.frame.original_guest).width,
              s.format.original_graph.SourceExtent(id.original_resolved_guest_object,s.frame.original_guest).height,
              s.format.original_graph.ColorFormat(id.original_resolved_guest_object,s.format.color)==NativeColorFormat::Rgba16Float);
        } else {
          if(!texture.uploaded)return false;
          const auto& pass=s.material_lookup.at(key)->pipeline;
          const bool world=pass.original_view_space_verified &&
              pass.original_view_space==VerifiedOriginalViewSpace::MainPerspective &&
              pass.source_main_view.has_value();
          BindOriginalPixelImage2D(s.device,binding,*texture.uploaded,image->slot,
                                   captured.pixel_fetch[image->slot],(controls.point_slots&bit)!=0,
                                   (pass.original_xbox_cube_gamma_mask&bit)!=0,
                                   world?s.world_anisotropy:-1,
                                   s.AcquirePcPrompt(*texture.uploaded,pass,captured.pixel_fetch[image->slot]));
        }
        bound|=bit;
      }
      if(bound!=controls.sampled_slots)return false;
    }
  } catch (...) {return false;}
  s.packed_constants=std::move(prepared_constants);
  s.first_draw_depth_bound=draws.front().draw.guest_vs_matrices->original_depth_guest_object!=0;
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
  if(!s.format.original_graph.active() && !s.scene.SetDepthBound(s.first_draw_depth_bound)) {
    s.scene.AbortScene();return false;
  }
  s.resolve_reuse.Reset();
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
  return BindSurfaceImpl(model,ordinal,false);
}
bool DiligentEorDirectBackend::BindOriginalRuntimeSurface(uint32_t model,uint32_t ordinal) {
  return BindSurfaceImpl(model,ordinal,true);
}
bool DiligentEorDirectBackend::BindSurfaceImpl(uint32_t model,uint32_t ordinal,bool runtime) {
  auto& s=*state_;
  // Every Draw begins from an unbound PSO/constant/sampler state.
  s.selected=nullptr;s.active={};
  s.constants_ready=s.program_ready=s.textures_ready=false;
  s.requested_model=0;s.requested_surface=UINT32_MAX;s.surface_ready=false;
  if(!s.scene_open || s.finished || !model || ordinal==UINT32_MAX)return false;
  // A source model may own many effects, but the first keyed pass is
  // sufficient to prove that THIS exact model/surface is resident.
  auto item=s.material_lookup.lower_bound({model,ordinal,0u,0u,false,false,0u});
  while(item!=s.material_lookup.end() && std::get<0>(item->first)==model &&
        std::get<1>(item->first)==ordinal && std::get<5>(item->first)!=runtime)++item;
  if(item==s.material_lookup.end() || std::get<0>(item->first)!=model ||
     std::get<1>(item->first)!=ordinal || std::get<5>(item->first)!=runtime)return false;
  s.requested_model=model;s.requested_surface=ordinal;s.requested_runtime=runtime;
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
      {s.requested_model,s.requested_surface,effect,pass,!s.scene.DepthBound(),s.requested_runtime,
       s.format.original_graph.active()?s.scene.ActiveOriginalColor():0u});
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
  const auto pipeline=s.pipeline_cache.find(PassKey(*s.selected));
  const auto binding=s.material_bindings.find(PassKey(*s.selected));
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
  const auto found=s.material_bindings.find(PassKey(*s.selected));
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
                                  resident.vertex_count,
                                  s.selected->pipeline.exact_render_state.PrimitiveTopology==Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST))
    return false;
  const auto& slots=resident.host_vertex_slots;
  const auto& layout=s.selected->pipeline.exact_host_input_layout;
  // Different passes of the same mesh may consume different streams.
  // Keep the original binding numbers even when a pass skips a slot.
  const bool is_font=resident.original_dynamic_font_verified;
  const bool runtime=resident.original_runtime_draw_verified;
  if(runtime!=s.selected->pipeline.original_runtime_draw_verified ||
     (runtime && (resident.original_runtime_frame!=draw.source_frame ||
                  resident.original_runtime_ordinal!=draw.source_ordinal)))return false;
  if(is_font &&
     (resident.original_font_frame!=draw.source_frame ||
      resident.original_font_draw_ordinal!=draw.source_ordinal ||
      !s.selected->pipeline.original_dynamic_font_vertex_layout_verified))
    return false;
  const uint32_t used_slots=is_font ?
      SourceFontSlotMask(layout,slots) :
      runtime ? SourceRuntimeSlotMask(layout,slots) : SourceVertexSlotMask(layout,slots);
  if(!used_slots)return false;
  const auto binding_count=std::bit_width(used_slots);
  std::array<Diligent::IBuffer*,16> buffers{};
  std::array<Diligent::Uint64,16> offsets{};
  for(uint32_t i=0;i<binding_count;++i) {
    if(!(used_slots & (1u << i)))continue;
    const auto kind=s.meshes.KindOf(slots[i]);
    // Several input elements can share a slot in a verified layout.
    // Each must independently agree with the SAME physical allocation.
    for(const auto& e:layout)if(e.BufferSlot==i) {
      const auto expected=OriginalVertexStorageKind(i,e.InputIndex,
          e.ValueType==Diligent::VT_UINT32,runtime,is_font);
      if(expected==UINT32_MAX || expected!=kind)return false;
    }
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
    if(packed==s.packed_constants.end())return false;
    auto* resources=is_font ? resident.font_bindings.RawPtr() :
        packed->second.draw_bindings ? packed->second.draw_bindings.RawPtr() : s.active.bindings.RawPtr();
    if(!resources || !matrices || !draw.guest_shader_constants)return false;
    const auto& regs=*draw.guest_shader_constants;
    const auto& constants=packed->second;
    // The scalar Hulk AOT ABI retains c0..c255, including holes and
    // relatively indexed registers. Legacy translators can use compact UBOs.
    const std::span<const float> vs=constants.vs_abi==FloatRegisterAbi::Direct256 && constants.vertex.empty()
        ? std::span<const float>(regs.vertex) : std::span<const float>(constants.vertex);
    const std::span<const float> ps=constants.ps_abi==FloatRegisterAbi::Direct256
        ? std::span<const float>(regs.pixel) : std::span<const float>(constants.pixel);
    if(!s.bind_constants(resources,s.vertex,s.pixel,draw,*matrices,regs,vs,ps)) {
      s.constants_ready=false;
      s.program_ready=false;
      s.textures_ready=false;
      return false;
    }
    if(constants.vs_abi==FloatRegisterAbi::Direct256)
      s.shaders.BindDirectFloatRegisters(s.context,resources,aot::Stage::Vertex,vs);
    if(constants.ps_abi==FloatRegisterAbi::Direct256)
      s.shaders.BindDirectFloatRegisters(s.context,resources,aot::Stage::Pixel,ps);
    const auto& raster=packed->second.raster;
    const auto& clip=raster.scissor;
    const Diligent::Viewport original_viewport{
        raster.x,raster.y,raster.width,raster.height,raster.min_depth,raster.max_depth};
    const Diligent::Rect original_scissor{
        int(clip.left),int(clip.top),int(clip.left+clip.width),int(clip.top+clip.height)};
    const auto extent=s.scene.ActiveHostExtent();
    if(!extent.width || !extent.height)return false;
    s.context->SetViewports(1,&original_viewport,extent.width,extent.height);
    s.context->SetScissorRects(1,&original_scissor,extent.width,extent.height);
    // A variant was built and bound before BeginScene from this exact
    // original frame/ordinal; never bind its SRB with the base PSO.
    s.context->SetPipelineState(constants.variant_pipeline ?
        constants.variant_pipeline.RawPtr() : s.active.pipeline.RawPtr());
    if(regs.fixed_graphics_state) {
      const auto depth=regs.fixed_graphics_state->ReadOriginalXenosRegister(0x2200u);
      if(depth && (*depth&1u)) {
        const auto reference=regs.fixed_graphics_state->ReadOriginalXenosRegister(0x210du);
        if(!reference)return false;
        s.context->SetStencilRef(*reference&255u);
      }
    }
    // Dynamic resolved textures have no archive hash and no SRV before
    // their actual native Resolve. Bind only the graph-owned completed image.
    for(const auto& image:s.selected->original_textures) {
      const auto guest=image.identity.original_resolved_guest_object;
      if(!guest)continue;
      auto* srv=s.scene.IntermediateColorSRV(guest);
      auto* variable=resources->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
          image.identity.exact_shader_variable_name.c_str());
      if(!srv || !variable)return false;
      variable->Set(srv,Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    s.context->CommitShaderResources(resources,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    s.context->SetVertexBuffers(0,binding_count,
        buffers.data(),offsets.data(),
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    s.context->SetIndexBuffer(index,0,Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DrawIndexedAttribs command(
        draw.index_count,Diligent::VT_UINT32,Diligent::DRAW_FLAG_NONE,
        1,draw.first_index,static_cast<Diligent::Uint32>(draw.base_vertex));
    const auto color_mask=regs.fixed_graphics_state ?
        regs.fixed_graphics_state->ReadOriginalXenosRegister(0x2104u) : std::nullopt;
    s.resolve_reuse.DrawColorWritten(s.scene.ActiveOriginalColor(),color_mask);
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
