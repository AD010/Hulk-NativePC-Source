#pragma once
// Runtime producer for the scalar Hulk AOT ABI. No original game bytes are
// included here. Each accepted material is proved by the original live XDK
// Draw, its ShaderContainer hashes, exact archive geometry and texture owner.
#include "eor_diligent_aot_backend.h"
#include "eor_native_draw_geometry.h"
#include "eor_original_shader_samplers.h"
#include "eor_spirv_texture_bindings.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace hulk::eor::gpu {
namespace live {
struct ScalarInput {
  uint32_t location=0,components=0;
  bool uint_bits=false;
};
struct ScalarInterface {
  std::vector<ScalarInput> inputs;
  uint32_t shared_members=0;
  bool supported=true;
  bool has_kill=false;
};
// Reflect the actual scalar SPIR-V interface, not a guessed layout based on
// the ShaderEffects name. A shared member 0 is a source boolean register;
// this producer does not claim to know it until an XDK boolean ABI exists.
inline ScalarInterface InspectScalarInterface(const aot::Blob& shader) {
  ScalarInterface out;
  struct Type {uint32_t op=0,a=0,b=0;};
  struct Variable {uint32_t type=0,storage=0;};
  std::map<uint32_t,Type> types;
  std::map<uint32_t,Variable> vars;
  std::map<uint32_t,uint32_t> loc,constants;
  std::map<uint32_t,std::string> names;
  std::set<uint32_t> builtins;
  auto words=std::span<const uint32_t>(shader.spirv);
  if(words.size()<5)throw std::invalid_argument("Missing scalar SPIR-V");
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    if(!n || n>words.size()-i)throw std::invalid_argument("Malformed scalar SPIR-V");
    const auto* w=words.data()+i;
    if(op==252u)out.has_kill=true;
    if(op==5u && n>=3u) {
      std::string value;
      bool terminated=false;
      for(size_t j=2;j<n && !terminated;++j)
        for(unsigned b=0;b<4;++b) {
          const char c=static_cast<char>((w[j]>>(8*b))&255u);
          if(!c){terminated=true;break;}
          value.push_back(c);
        }
      if(!terminated)throw std::invalid_argument("Unterminated scalar SPIR-V name");
      names.emplace(w[1],std::move(value));
    } else if(op==71u && n>=3u) {
      if(w[2]==30u && n==4u)loc.emplace(w[1],w[3]);
      if(w[2]==11u && n==4u)builtins.insert(w[1]);
    } else if((op==21u || op==23u || op==32u) && n==4u) {
      types.emplace(w[1],Type{op,w[2],w[3]});
    } else if(op==22u && n==3u) {
      types.emplace(w[1],Type{op,w[2],0});
    } else if(op==59u && n>=4u) {
      vars.emplace(w[2],Variable{w[1],w[3]});
    } else if(op==43u && n==4u)constants.emplace(w[2],w[3]);
    i+=n;
  }
  uint32_t shared=0;
  std::set<uint32_t> seen;
  for(const auto& [id,var]:vars) {
    const auto name=names.find(id);
    if(var.storage==2u && name!=names.end() && name->second=="HulkSharedConstants") {
      if(shared)throw std::invalid_argument("Duplicate scalar shared block");
      shared=id;
    }
    if(shader.stage==aot::Stage::Pixel && var.storage==3u && loc.contains(id) &&
       loc.at(id)!=0u)out.supported=false; // MRT is not a single-target pass.
    if(shader.stage!=aot::Stage::Vertex || var.storage!=1u)continue;
    if(builtins.contains(id)){out.supported=false;continue;}
    const auto l=loc.find(id);
    if(l==loc.end() || l->second>=16u || !seen.insert(l->second).second ||
       name==names.end() || name->second!="in.var.ATTRIB"+std::to_string(l->second))
      throw std::invalid_argument("Unverified scalar vertex location");
    const auto ptr=types.at(var.type);
    if(ptr.op!=32u || ptr.a!=1u)throw std::invalid_argument("Invalid scalar vertex pointer");
    const auto vector=types.at(ptr.b);
    if(vector.op!=23u || vector.b<1u || vector.b>4u)
      throw std::invalid_argument("Unsupported scalar vertex type");
    const auto scalar=types.at(vector.a);
    const bool bits=scalar.op==21u && scalar.a==32u && scalar.b==0u;
    if(!bits && !(scalar.op==22u && scalar.a==32u))
      throw std::invalid_argument("Unsupported scalar vertex component");
    out.inputs.push_back({l->second,vector.b,bits});
  }
  if(shared) {
    for(size_t i=5;i<words.size();) {
      const auto n=words[i]>>16,op=words[i]&65535u;
      const auto* w=words.data()+i;
      if((op==65u || op==66u) && n>=5u && w[3]==shared) {
        const auto member=constants.find(w[4]);
        if(member==constants.end() || member->second>=5u)out.supported=false;
        else out.shared_members|=1u<<member->second;
      } else if((op==61u && n>=4u && w[3]==shared) ||
                (op==67u && n>=5u && w[3]==shared))
        out.supported=false;
      i+=n;
    }
    if(!out.shared_members)out.supported=false;
  }
  // Boolean member 0 is allowed only when the producer has captured the
  // original XDK boolean register file for this exact Draw.
  std::sort(out.inputs.begin(),out.inputs.end(),[](auto a,auto b){return a.location<b.location;});
  return out;
}
inline std::optional<uint32_t> SemanticLocation(uint32_t usage,uint32_t index) noexcept {
  if(usage==0u && index==0u)return 0u;
  if(usage==3u && index==0u)return 1u;
  if(usage==6u && index==0u)return 2u;
  if(usage==7u && index==0u)return 3u;
  if(usage==5u && index<4u)return 4u+index;
  if(usage==10u && index==0u)return 8u;
  if(usage==2u && index==0u)return 9u;
  if(usage==1u && index==0u)return 10u;
  if(usage==10u && index==1u)return 11u;
  if(usage==5u && index>=4u && index<7u)return 8u+index;
  // Location 15 has two upstream semantics; never guess which one.
  return {};
}
inline std::vector<Diligent::LayoutElement> OriginalScalarMeshLayout(
    const xdk::RawDraw& call,const model::UploadedNativeMesh& mesh,
    const ScalarInterface& shader) {
  using namespace Diligent;
  if(!shader.supported || !call.vertex_declaration ||
     call.vertex_declaration->streams!=1u ||
     mesh.original_attribute_declarations.size()!=mesh.decoded_attribute_buffers.size()+2u)
    throw std::invalid_argument("Missing exact single-stream scalar mesh declaration");
  std::vector<LayoutElement> output;
  auto byte=[](std::byte b){return std::to_integer<uint32_t>(b);};
  for(const auto& input:shader.inputs) {
    uint32_t offset=0;
    size_t count=0;
    for(size_t slot=0;slot+1<mesh.original_attribute_declarations.size();++slot) {
      const auto& source=mesh.original_attribute_declarations[slot];
      const auto type=byte(source[0]),usage=byte(source[1]),index=byte(source[2]),end=byte(source[3]);
      const auto location=SemanticLocation(usage,index);
      if(location && *location==input.location) {
        const auto guest=std::find_if(call.vertex_declaration->elements.begin(),
            call.vertex_declaration->elements.end(),[&](const auto& item){
          return item.stream==0u && item.byte_offset==offset &&
                 ((item.original_packed_usage>>16)&255u)==usage &&
                 ((item.original_packed_usage>>8)&255u)==index;
        });
        if(guest==call.vertex_declaration->elements.end())
          throw std::invalid_argument("Live XDK declaration lacks original scalar input");
        // Only equivalently decoded formats. The upstream scalar shader
        // uses asfloat(uint4) for NORMAL/TANGENT when R11G11B10 is off.
        // The decoded float4 allocation already contains those exact bits.
        const auto format=guest->original_packed_type;
        const bool normal=(usage==3u || usage==6u) && type==0x10u && end-offset==4u;
        if(slot==0u) {
          if(input.uint_bits || input.components!=4u || type!=2u ||
             guest->original_packed_type!=0x2a23b9u)
            throw std::invalid_argument("Unsupported original position ABI");
        } else if(input.uint_bits) {
          const bool blend_indices=usage==2u && index==0u && type==0x11u &&
              format==0x1a2286u && end-offset==4u;
          if(input.components!=4u && (normal || blend_indices))
            throw std::invalid_argument("Invalid scalar uint4 vertex input");
          if(!(normal && format==0x1a2186u) && !blend_indices)
            throw std::invalid_argument("Original integer vertex attribute type mismatch");
        } else if(!((usage==5u && type==0x2eu && format==0x1a2360u) ||
                    (usage==10u && type==0x10u && guest->original_packed_type==0x182886u) ||
                    (usage==1u && type==0x10u && format==0x1a2086u))) {
          throw std::invalid_argument("Unsupported original float attribute ABI");
        }
        LayoutElement element{input.location,static_cast<uint32_t>(slot),
            slot==0u ? 3u : input.components,input.uint_bits ? VT_UINT32 : VT_FLOAT32,False};
        element.RelativeOffset=0;
        element.Stride=slot==0u ? 12u : 16u;
        output.push_back(element);
        ++count;
      }
      if(end<=offset || end>mesh.original_vertex_stride)
        throw std::invalid_argument("Invalid archived scalar declaration range");
      offset=end;
    }
    if(count!=1u)throw std::invalid_argument("Ambiguous archive scalar vertex input");
  }
  if(output.empty() || output.front().InputIndex!=0u)
    throw std::invalid_argument("Original scalar vertex position missing");
  return output;
}
inline VerifiedOriginalGraphicsPass OriginalScalarPass(
    const xdk::RawDraw& call,const aot::Digest& vs,const aot::Digest& ps,
    const VerifiedSceneFormat& format) {
  using namespace Diligent;
  if(!call.observed_live_eor_effect_pass || !call.observed_vs_matrices ||
     !call.observed_shader_constants || !call.observed_shader_constants->fixed_graphics_state)
    throw std::invalid_argument("Original scalar material state not captured");
  VerifiedOriginalGraphicsPass pass;
  pass.original_effect_hash=call.observed_live_eor_effect_pass->effect_hash;
  pass.original_pass_index=call.observed_live_eor_effect_pass->pass_index;
  pass.original_vertex_shader_sha256=vs;pass.original_pixel_shader_sha256=ps;
  pass.original_depth_unbound=!call.observed_vs_matrices->original_depth_guest_object;
  pass.original_color_owner=format.original_graph.active()?call.observed_vs_matrices->original_rt0_guest_object:0u;
  const auto color=format.original_graph.ColorFormat(pass.original_color_owner,format.color);
  auto& state=pass.exact_render_state;
  state.NumRenderTargets=1;
  state.RTVFormats[0]=color==NativeColorFormat::Rgba8 ?
      TEX_FORMAT_RGBA8_UNORM : color==NativeColorFormat::Rgba16Float ?
      TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_UNKNOWN;
  state.DSVFormat=pass.original_depth_unbound ? TEX_FORMAT_UNKNOWN :
      format.depth==NativeDepthFormat::D24S8 ? TEX_FORMAT_D24_UNORM_S8_UINT : TEX_FORMAT_UNKNOWN;
  if(!pass.original_depth_unbound && state.DSVFormat==TEX_FORMAT_UNKNOWN)
    throw std::invalid_argument("Original scalar depth attachment unsupported");
  state.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  const auto derived=DeriveOriginalXdkGraphicsPipelineState(
      *call.observed_shader_constants->fixed_graphics_state,state);
  if(!derived)throw std::invalid_argument("Original scalar fixed graphics state unsupported");
  state=*derived;
  pass.original_render_state_verified=true;
  return pass;
}
inline std::vector<VerifiedNativeMaterialImage> OriginalScalarImages(
    const xdk::RawDraw& call,const aot::Digest& vs,const aot::Digest& ps,
    const aot::ShaderBank& bank,const aot::OriginalXdkShaderSamplers& samplers,
    uint32_t model,uint32_t surface,uint16_t source_mask,
    DiligentOriginalTextureResidency& residency,const VerifiedSceneFormat& scene) {
  const auto* v=bank.Find(vs,aot::Stage::Vertex);
  const auto* p=bank.Find(ps,aot::Stage::Pixel);
  if(!v || !p || !call.observed_vs_matrices || !call.observed_shader_constants ||
     !call.observed_live_pixel_shader_header ||
     !call.observed_live_pixel_shader_header->pixel_sampling ||
     !samplers.Find(vs,aot::Stage::Vertex) || !samplers.Find(ps,aot::Stage::Pixel) ||
     !aot::InspectSpirvImageResources(v->spirv,aot::Stage::Vertex).empty())
    throw std::invalid_argument("Unsupported original scalar sampler stages");
  const auto& sampling=*call.observed_live_pixel_shader_header->pixel_sampling;
  if(sampling.source!=ps || (sampling.sampled_slots&~source_mask))
    throw std::invalid_argument("Original scalar sampler program differs from live material");
  uint16_t used=0;
  std::vector<VerifiedNativeMaterialImage> out;
  for(const auto& image:aot::InspectSpirvImageResources(p->spirv,aot::Stage::Pixel)) {
    const auto slot=aot::DecodeFixedAotImageSlot(image.exact_variable_name);
    if(!slot || slot->slot>=16u || slot->dimension==2u || slot->dimension>3u || image.spirv_dim!=slot->dimension ||
       image.image_arrayed || image.image_multisampled)
      throw std::invalid_argument("Unsupported original scalar image dimension");
    const auto bit=static_cast<uint16_t>(1u<<slot->slot);
    const auto& captured=*call.observed_shader_constants;
    if((used&bit) || !(sampling.sampled_slots&bit) || !(captured.valid_pixel_fetch&bit) ||
       !call.observed_vs_matrices->original_texture_guest_objects[slot->slot])
      throw std::invalid_argument("Unverified scalar live XDK texture slot");
    const auto& declarations=*samplers.Find(ps,aot::Stage::Pixel);
    if(std::count_if(declarations.begin(),declarations.end(),[&](const auto& decl){
         return decl.original_sampler_register==slot->slot;
       })!=1)
      throw std::invalid_argument("Original scalar CTAB slot missing");
    // The existing scalar sampler binder accepts unsigned linear
    // formats only. Do not silently convert signed/gamma fetches.
    const auto signs=captured.pixel_fetch[slot->slot][0]&1023u;
    if(signs!=2u && !(signs==254u && slot->dimension==3u))
      throw std::invalid_argument("Unsupported original scalar color space");
    const auto guest=call.observed_vs_matrices->original_texture_guest_objects[slot->slot];
    if(scene.original_graph.HasResolve(guest)) {
      if(slot->dimension!=1u)throw std::invalid_argument("Original resolved image must be 2D");
      VerifiedEorSampler binding{model,surface,call.observed_live_eor_effect_pass->effect_hash,
        call.observed_live_eor_effect_pass->pass_index,0u,slot->slot,OriginalSamplerStage::Pixel,
        image.exact_variable_name,true,true,true,guest};
      out.push_back({std::move(binding),OriginalTextureColorSpace::Linear,OriginalTextureViewKind::Native2D});
      used|=bit;
      continue;
    }
    const auto hash=call.original_pixel_texture_keys[slot->slot];
    if(!hash)throw std::invalid_argument("Original static sampled image has no archive identity");
    const auto view=slot->dimension==3u ? OriginalTextureViewKind::NativeCube :
                   slot->dimension==0u ? OriginalTextureViewKind::Native1D :
                                         OriginalTextureViewKind::Native2D;
    auto uploaded=residency.Acquire(hash,OriginalTextureColorSpace::Linear,view);
    if(!uploaded || uploaded->original_texture_hash!=hash || !uploaded->SRV())
      throw std::invalid_argument("Original scalar mip chain not resident");
    VerifiedEorSampler binding{model,surface,call.observed_live_eor_effect_pass->effect_hash,
      call.observed_live_eor_effect_pass->pass_index,hash,slot->slot,OriginalSamplerStage::Pixel,
      image.exact_variable_name,true,true,true};
    out.push_back({std::move(binding),OriginalTextureColorSpace::Linear,view});
    used|=bit;
  }
  if(used!=sampling.sampled_slots)
    throw std::invalid_argument("Incomplete original scalar sampler mapping");
  return out;
}
struct ProducedWorldMaterial {
  VerifiedNativeMaterialPass material;
  native::VerifiedDrawSource draw;
};
// DrawVertices and DrawIndexedVertices use the same verified archive
// mesh. For nonindexed lists/fans, locate a host triangle-index window
// whose effective vertex sequence equals EVERY original source vertex.
struct LinearizedOriginalDraw {uint32_t first=0,count=0;int32_t base=0;};
inline std::optional<LinearizedOriginalDraw> FindOriginalLinearizedDraw(
    const xdk::VerticesXdkCall& original,const native::ExactOriginalGuestMesh& exact,
    const model::UploadedNativeMesh& mesh) noexcept {
  const auto count=xdk::VerifiedTriangleListCount(original.primitive,original.vertex_count);
  const int64_t first=int64_t(original.start_vertex)-exact.guest_first_vertex;
  if(!count || first<0 || uint64_t(first)+original.vertex_count>mesh.vertex_count ||
     mesh.host_indices.size()!=mesh.index_count || *count>mesh.index_count)return {};
  for(uint32_t i=0;i<=mesh.index_count-*count;++i) {
    const int64_t base=first-mesh.host_indices[i];
    if(base<INT32_MIN || base>INT32_MAX)continue;
    bool equal=true;
    for(uint32_t j=0;j<*count;++j) {
      const auto position=original.primitive==6u ?
          xdk::OriginalTriangleFanVertex(j/3u,j%3u) : j;
      if(int64_t(mesh.host_indices[i+j])+base!=first+position) {
        equal=false;break;
      }
    }
    // Equivalent duplicate windows generate the exact same effective
    // vertex sequence; choose the first without altering any source index.
    if(equal)return LinearizedOriginalDraw{i,*count,static_cast<int32_t>(base)};
  }
  return {};
}
inline ProducedWorldMaterial ProduceOriginalWorldMaterial(
    const xdk::RawDraw& call,uint64_t frame,uint32_t ordinal,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank,const aot::EffectCatalog& effects,
    const aot::OriginalVirtualShaderIdentity& identities,
    const aot::OriginalXdkShaderSamplers& samplers,const VerifiedSceneFormat& format,
    DiligentOriginalTextureResidency& residency) {
  const auto indexed=xdk::DecodeOriginalIndexedXdkCall(call);
  const auto vertices=xdk::DecodeOriginalVerticesXdkCall(call);
  if((!indexed && !vertices) || (indexed && indexed->primitive!=4u) ||
     (vertices && !xdk::VerifiedTriangleListCount(vertices->primitive,vertices->vertex_count)) ||
     !call.captured_vertex_bindings ||
     !call.vertex_declaration || !call.observed_vs_matrices ||
     !call.observed_live_vertex_shader_header || !call.observed_live_pixel_shader_header ||
     !call.observed_live_eor_effect_pass || !call.original_engine_shader_method ||
     !xdk::ValidOriginalEorSourceRenderItem(call) || !xdk::ValidOriginalDecalGeometryBinding(call) ||
     !xdk::ValidOriginalDecalItemMaterialBinding(call) || !xdk::ValidOriginalShaderPassStageBinding(call) ||
     !xdk::ValidOriginalShaderPassSamplerStateAtDraw(call))
    throw std::invalid_argument("Original indexed engine Draw not fully captured");
  const auto& live=*call.observed_vs_matrices;
  const auto& method=*call.original_engine_shader_method;
  if(!method.original_shader_method_vtable_verified ||
     !method.original_pass_vs_ps_xdk_binding_verified ||
     !method.original_pass_sampler_slots_verified ||
     !method.original_pass_live_texture_objects_present)
    throw std::invalid_argument("Original indexed engine material setter not captured");
  const auto vs=aot::ResolveOriginalLiveXdkShaderSource(identities,
      *call.observed_live_vertex_shader_header,live.original_vs_guest_object,
      aot::Stage::Vertex,frame,ordinal);
  const auto ps=aot::ResolveOriginalLiveXdkShaderSource(identities,
      *call.observed_live_pixel_shader_header,live.original_ps_guest_object,
      aot::Stage::Pixel,frame,ordinal);
  const auto effect=*call.observed_live_eor_effect_pass;
  if(!vs || !ps || !effects.HasPair(effect.effect_hash,effect.pass_index,*vs,*ps))
    throw std::invalid_argument("Original indexed ShaderEffects identity mismatch");
  const auto* vertex=bank.Find(*vs,aot::Stage::Vertex);
  const auto* pixel=bank.Find(*ps,aot::Stage::Pixel);
  if(!vertex || !pixel)throw std::invalid_argument("Original indexed AOT pair missing");
  const auto vertex_interface=InspectScalarInterface(*vertex);
  const auto pixel_interface=InspectScalarInterface(*pixel);
  if(((vertex_interface.shared_members|pixel_interface.shared_members)&1u) &&
     (!call.observed_shader_constants || !xdk::PackOriginalScalarBooleans(*call.observed_shader_constants)))
    throw std::invalid_argument("Missing original boolean register file");
  if(!vertex_interface.supported || !pixel_interface.supported)
    throw std::invalid_argument("Unsupported original shared constants or MRT shader");
  const auto exact=native::IdentifyExactOriginalGuestMesh(call,residents);
  if(!exact)throw std::invalid_argument("Original indexed VB/IB has no unique archived mesh");
  const model::UploadedOriginalModel* owner=nullptr;
  const model::UploadedNativeMesh* mesh=nullptr;
  for(const auto* model:residents)if(model && model->original_resource_hash==exact->model_hash) {
    if(owner)throw std::invalid_argument("Ambiguous original model owner");
    owner=model;
    for(const auto& candidate:model->meshes.meshes())
      if(candidate.original_header_offset==exact->mesh_header_offset)mesh=&candidate;
  }
  if(!owner || !mesh)throw std::invalid_argument("Exact original GPU mesh not resident");
  const auto sampling=call.observed_live_pixel_shader_header->pixel_sampling;
  if(!sampling || !sampling->sampled_slots)
    throw std::invalid_argument("Untextured surface requires original engine material ID");
  uint16_t archive_mask=sampling->sampled_slots;
  for(uint32_t slot=0;slot<16;++slot)
    if(format.original_graph.HasResolve(live.original_texture_guest_objects[slot]))
      archive_mask&=static_cast<uint16_t>(~(1u<<slot));
  const auto surface=native::IdentifyLiveOriginalXdkMaterialSurface(*owner,call,archive_mask);
  if(!surface || *surface>=owner->original_surface_candidates.size())
    throw std::invalid_argument("Original material surface is ambiguous");
  LinearizedOriginalDraw host_draw;
  if(indexed) {
    const int64_t base=int64_t(indexed->base_vertex)-exact->guest_first_vertex;
    if(base<INT32_MIN || base>INT32_MAX || indexed->first_index<exact->guest_first_index)
      throw std::invalid_argument("Original indexed host rebase outside signed range");
    host_draw={indexed->first_index-exact->guest_first_index,indexed->index_count,
               static_cast<int32_t>(base)};
  } else {
    const auto linear=FindOriginalLinearizedDraw(*vertices,*exact,*mesh);
    if(!linear)throw std::invalid_argument("Original DrawVertices triangle sequence not resident");
    host_draw=*linear;
  }
  ProducedWorldMaterial out;
  auto& pass=out.material.original_pipeline;
  pass=OriginalScalarPass(call,*vs,*ps,format);
  pass.exact_host_input_layout=OriginalScalarMeshLayout(call,*mesh,vertex_interface);
  pass.original_host_vertex_layout_verified=true;
  pass.original_scalar_alpha_test_verified=pixel_interface.has_kill &&
      (pixel_interface.shared_members&24u)==24u;
  const auto alpha=ResolveOriginalScalarAlpha(*call.observed_shader_constants->fixed_graphics_state);
  if(!alpha || (alpha->flags && !pass.original_scalar_alpha_test_verified))
    throw std::invalid_argument("Original scalar alpha-test shader unsupported");
  out.material.original_model_hash=owner->original_resource_hash;
  out.material.original_surface_ordinal=*surface;
  out.material.original_images=OriginalScalarImages(call,*vs,*ps,bank,samplers,
      owner->original_resource_hash,*surface,method.original_pass_sampler_slot_mask,residency,format);
  for(const auto& image:out.material.original_images) {
    const auto slot=image.original_binding.original_fetch_register;
    if(image.shader_reflected_view==OriginalTextureViewKind::NativeCube &&
       (call.observed_shader_constants->pixel_fetch[slot][0]&1023u)==254u)
      pass.original_xbox_cube_gamma_mask|=uint16_t(1u<<slot);
  }
  pass.material_pass_verified=true;
  auto& d=out.draw;
  d.original_model_hash=owner->original_resource_hash;
  d.original_mesh_header_offset=mesh->original_header_offset;
  d.original_surface_ordinal=*surface;
  d.original_surface_name=owner->original_surface_candidates[*surface].original_material_name;
  d.original_effect_hash=effect.effect_hash;d.original_pass_index=effect.pass_index;
  d.vertex_source_sha256=*vs;d.pixel_source_sha256=*ps;
  d.vertex_shader_id=live.original_vs_guest_object;d.pixel_shader_id=live.original_ps_guest_object;
  d.first_index=host_draw.first;
  d.index_count=host_draw.count;d.host_base_vertex=host_draw.base;
  d.original_linear_draw_verified=vertices.has_value();
  d.original_frame_sequence=frame;d.original_call_ordinal=ordinal;
  d.original_vs_matrices=live;d.original_shader_constants=call.observed_shader_constants;
  d.original_live_vertex_shader_header=call.observed_live_vertex_shader_header;
  d.original_live_pixel_shader_header=call.observed_live_pixel_shader_header;
  d.engine_draw_verified=d.mesh_material_verified=d.host_vertex_layout_verified=d.render_state_verified=true;
  for(const auto& image:out.material.original_images)d.original_sampler_slots.push_back(image.original_binding);
  return out;
}

inline std::optional<model::VerifiedAttributeLayout> RuntimeAttribute(
    const xdk::UsaVertexElement& e) noexcept {
  using E=model::AttributeEncoding;
  uint8_t n=0; E encoding=E::BigFloat32;
  switch(e.original_packed_type) {
    case 0x2c83a4u:n=1;break;
    case 0x2c23a5u:n=2;break;
    case 0x2a23b9u:n=3;break;
    case 0x1a23a6u:n=4;break;
    case 0x182886u:n=4;encoding=E::BigArgb8;break;
    case 0x1a2286u:n=4;encoding=E::UInt8;break;
    case 0x1a2086u:n=4;encoding=E::UNorm8;break;
    case 0x1a2186u:n=4;encoding=E::SNorm8;break;
    case 0x2c2159u:n=2;encoding=E::BigSNorm16;break;
    case 0x1a215au:n=4;encoding=E::BigSNorm16;break;
    case 0x2c2059u:n=2;encoding=E::BigUNorm16;break;
    case 0x1a205au:n=4;encoding=E::BigUNorm16;break;
    case 0x2c235fu:n=2;encoding=E::BigFloat16;break;
    case 0x1a2360u:n=4;encoding=E::BigFloat16;break;
    default:return {};
  }
  return model::VerifiedAttributeLayout{e.byte_offset,n,encoding};
}
// Rebuild the exact effective vertex sequence of an ordinary buffered XDK
// Draw. No archive model is required for a transient or deformed stream.
inline DecodedRuntimeGeometry DecodeOriginalRuntimeGeometry(
    const xdk::RawDraw& call,const ScalarInterface& shader) {
  if(!call.captured_vertex_bindings || !call.vertex_declaration ||
     !shader.supported || shader.inputs.empty() || shader.inputs.front().location!=0u)
    throw std::invalid_argument("Missing transient vertex declaration");
  auto indexed=xdk::DecodeOriginalIndexedXdkCall(call);
  auto vertices=xdk::DecodeOriginalVerticesXdkCall(call);
  if(!indexed && !vertices)throw std::invalid_argument("Unsupported transient Draw kind");
  const uint32_t primitive=indexed?indexed->primitive:vertices->primitive;
  const uint32_t count=indexed?indexed->index_count:vertices->vertex_count;
  const auto triangle_count=xdk::VerifiedRuntimeTriangleListCount(primitive,count);
  if(!triangle_count || count>1048576u || *triangle_count>3145728u)
    throw std::invalid_argument("Unsupported transient topology or draw size");
  std::vector<uint32_t> sequence;sequence.reserve(count);
  if(indexed && (!call.index_binding || !call.source_ib ||
                 !call.index_binding->Matches(*call.source_ib)))
    throw std::invalid_argument("Unverified transient index buffer");
  for(uint32_t i=0;i<count;++i) {
    int64_t effective=0;
    if(indexed) {
      if(uint64_t(indexed->first_index)+i>UINT32_MAX)
        throw std::invalid_argument("Transient index offset overflow");
      const auto value=call.index_binding->Read(call.source_ib->bytes,indexed->first_index+i);
      if(!value)throw std::invalid_argument("Transient index outside source allocation");
      effective=int64_t(*value)+indexed->base_vertex;
    } else effective=int64_t(vertices->start_vertex)+i;
    if(effective<0 || effective>UINT32_MAX)
      throw std::invalid_argument("Transient effective vertex outside range");
    sequence.push_back(static_cast<uint32_t>(effective));
  }
  const auto [minimum,maximum]=std::minmax_element(sequence.begin(),sequence.end());
  const uint32_t first=*minimum;
  const uint64_t size=uint64_t(*maximum)-first+1u;
  if(size>1048576u)throw std::invalid_argument("Transient vertex span too large");
  for(auto& i:sequence)i-=first;
  DecodedRuntimeGeometry out;out.vertex_count=static_cast<uint32_t>(size);
  out.indices.reserve(*triangle_count);
  if(primitive==4u)out.indices=sequence;
  else if(primitive==5u)for(uint32_t i=0;i+2<count;++i) {
    out.indices.push_back(sequence[i+(i&1u)]);
    out.indices.push_back(sequence[i+1u-(i&1u)]);
    out.indices.push_back(sequence[i+2u]);
  } else if(primitive==6u)for(uint32_t i=1;i+1<count;++i) {
    out.indices.insert(out.indices.end(),{sequence[0],sequence[i],sequence[i+1]});
  } else if(primitive==13u)for(uint32_t i=0;i<count;i+=4) {
    out.indices.insert(out.indices.end(),{sequence[i],sequence[i+1],sequence[i+2],
                                         sequence[i],sequence[i+2],sequence[i+3]});
  }
  if(out.indices.size()!=*triangle_count)throw std::logic_error("Transient triangle count mismatch");
  std::map<uint32_t,model::NativeIndexedMesh> streams;
  for(const auto& input:shader.inputs) {
    const xdk::UsaVertexElement* element=nullptr;
    for(const auto& e:call.vertex_declaration->elements) {
      const auto loc=SemanticLocation((e.original_packed_usage>>16)&255u,
                                     (e.original_packed_usage>>8)&255u);
      if(loc && *loc==input.location) {
        if(element)throw std::invalid_argument("Duplicate transient input");
        element=&e;
      }
    }
    if(!element || element->stream>=16u)
      throw std::invalid_argument("Transient shader input has no declaration");
    const auto slot=element->stream;
    const auto& binding=call.vertex_bindings[slot];
    auto bytes=call.source_vertex_streams[slot];
    if(!bytes && slot==0u)bytes=call.source_vb0;
    if(!binding || !bytes || !binding->Matches(*bytes) ||
       !binding->stride || binding->stride>192u || binding->stride%4u ||
       (uint64_t(first)+size)*binding->stride>binding->length)
      throw std::invalid_argument("Transient vertex window outside source allocation");
    auto [it,inserted]=streams.try_emplace(slot);
    if(inserted) {
      auto& mesh=it->second;mesh.original_vertex_stride=binding->stride;
      mesh.positions.resize(out.vertex_count);
      const size_t begin=size_t(binding->offset)+size_t(first)*binding->stride;
      const size_t end=begin+size_t(size)*binding->stride;
      mesh.original_vertex_stream.assign(bytes->bytes.begin()+begin,bytes->bytes.begin()+end);
    }
    const auto format=RuntimeAttribute(*element);
    if(!format || (input.uint_bits && input.location!=1u &&
        input.location!=2u && input.location!=9u) ||
       (input.location==9u && (!input.uint_bits ||
          format->encoding!=model::AttributeEncoding::UInt8)))
      throw std::invalid_argument("Unsupported transient scalar vertex format");
    auto values=model::DecodeVerifiedAttribute(it->second,*format);
    if(format->components<4u)for(auto& v:values)v[3]=1.f;
    out.attributes.emplace_back();out.integer_attributes.emplace_back();
    if(input.location==9u) {
      auto& ints=out.integer_attributes.back();ints.reserve(values.size());
      for(const auto& v:values)ints.push_back({uint32_t(v[0]),uint32_t(v[1]),uint32_t(v[2]),uint32_t(v[3])});
    } else out.attributes.back()=std::move(values);
    Diligent::LayoutElement layout{input.location,static_cast<uint32_t>(out.layout.size()),4,
        input.uint_bits?Diligent::VT_UINT32:Diligent::VT_FLOAT32,Diligent::False};
    layout.RelativeOffset=0;layout.Stride=16;out.layout.push_back(layout);
  }
  return out;
}
struct ProducedRuntimeMaterial {
  VerifiedNativeMaterialPass material;
  DecodedRuntimeGeometry geometry;
};
inline ProducedRuntimeMaterial ProduceOriginalRuntimeMaterial(
    const xdk::RawDraw& call,uint64_t frame,uint32_t ordinal,
    const aot::ShaderBank& bank,const aot::EffectCatalog& effects,
    const aot::OriginalVirtualShaderIdentity& identities,
    const aot::OriginalXdkShaderSamplers& samplers,const VerifiedSceneFormat& format,
    DiligentOriginalTextureResidency& residency) {
  if(!call.observed_vs_matrices || !call.observed_shader_constants ||
     !call.observed_live_vertex_shader_header || !call.observed_live_pixel_shader_header ||
     !call.observed_live_eor_effect_pass || !call.original_engine_shader_method ||
     !xdk::ValidOriginalEorSourceRenderItem(call) || !xdk::ValidOriginalDecalGeometryBinding(call) ||
     !xdk::ValidOriginalDecalItemMaterialBinding(call) || !xdk::ValidOriginalShaderPassStageBinding(call) ||
     !xdk::ValidOriginalShaderPassSamplerStateAtDraw(call))
    throw std::invalid_argument("Transient engine Draw is not source proven");
  const auto& method=*call.original_engine_shader_method;
  const auto& live=*call.observed_vs_matrices;
  const auto& sampling=call.observed_live_pixel_shader_header->pixel_sampling;
  if(!method.original_shader_method_vtable_verified ||
     !method.original_pass_vs_ps_xdk_binding_verified ||
     !method.original_pass_material_context_guest || !sampling ||
     (sampling->sampled_slots && (!method.original_pass_sampler_slots_verified ||
                                  !method.original_pass_live_texture_objects_present)))
    throw std::invalid_argument("Transient material context is not source proven");
  const auto vs=aot::ResolveOriginalLiveXdkShaderSource(identities,
      *call.observed_live_vertex_shader_header,live.original_vs_guest_object,aot::Stage::Vertex,frame,ordinal);
  const auto ps=aot::ResolveOriginalLiveXdkShaderSource(identities,
      *call.observed_live_pixel_shader_header,live.original_ps_guest_object,aot::Stage::Pixel,frame,ordinal);
  const auto& effect=*call.observed_live_eor_effect_pass;
  if(!vs || !ps || !effects.HasPair(effect.effect_hash,effect.pass_index,*vs,*ps))
    throw std::invalid_argument("Transient shader identity mismatch");
  const auto* v=bank.Find(*vs,aot::Stage::Vertex);
  const auto* p=bank.Find(*ps,aot::Stage::Pixel);
  if(!v || !p)throw std::invalid_argument("Transient shader AOT pair missing");
  const auto vi=InspectScalarInterface(*v),pi=InspectScalarInterface(*p);
  if(!vi.supported || !pi.supported ||
     (((vi.shared_members|pi.shared_members)&1u) &&
       !xdk::PackOriginalScalarBooleans(*call.observed_shader_constants)))
    throw std::invalid_argument("Transient shader ABI unsupported");
  ProducedRuntimeMaterial out;
  out.geometry=DecodeOriginalRuntimeGeometry(call,vi);
  auto& material=out.material;
  material.original_model_hash=method.original_pass_material_context_guest;
  material.original_surface_ordinal=ordinal;
  auto& pass=material.original_pipeline;
  pass=OriginalScalarPass(call,*vs,*ps,format);
  pass.exact_host_input_layout=out.geometry.layout;
  pass.original_host_vertex_layout_verified=true;
  pass.original_runtime_draw_verified=true;
  pass.original_scalar_alpha_test_verified=pi.has_kill && (pi.shared_members&24u)==24u;
  const auto alpha=ResolveOriginalScalarAlpha(*call.observed_shader_constants->fixed_graphics_state);
  if(!alpha || (alpha->flags && !pass.original_scalar_alpha_test_verified))
    throw std::invalid_argument("Transient alpha-test shader unsupported");
  material.original_images=OriginalScalarImages(call,*vs,*ps,bank,samplers,
    material.original_model_hash,ordinal,method.original_pass_sampler_slot_mask,residency,format);
  for(auto& image:material.original_images) {
    image.original_binding.original_runtime_material=true;
    const auto slot=image.original_binding.original_fetch_register;
    if(image.shader_reflected_view==OriginalTextureViewKind::NativeCube &&
       (call.observed_shader_constants->pixel_fetch[slot][0]&1023u)==254u)
      pass.original_xbox_cube_gamma_mask|=uint16_t(1u<<slot);
  }
  pass.material_pass_verified=true;
  return out;
}
} // namespace live
} // namespace hulk::eor::gpu
