#include "eor_native_draw_geometry.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>

namespace hulk::eor::native {

// A guest XDK resource is commonly a shared allocation containing MANY EoR
// geometry objects. Never require its full byte length to equal one model
// mesh. Recover the *exact contiguous* original mesh VB+IB windows, including
// their offsets; require the current Draw to address those windows.
std::optional<ExactOriginalGuestMesh> IdentifyExactOriginalGuestMesh(
    const xdk::RawDraw& draw,
    std::span<const model::UploadedOriginalModel* const> residents) {
  if(!draw.observed_vs_matrices || !draw.source_vb0)return std::nullopt;
  const auto& state=*draw.observed_vs_matrices;
  const auto& vb=*draw.source_vb0;
  if(!vb.guest_object || !vb.guest_address ||
     vb.guest_object!=state.original_vertex_buffer_guest_objects[0] ||
     vb.bytes.empty() || vb.guest_size!=vb.bytes.size())return std::nullopt;
  const auto indexed=xdk::DecodeOriginalIndexedXdkCall(draw);
  const auto plain=xdk::DecodeOriginalVerticesXdkCall(draw);
  if(!indexed && !plain)return std::nullopt;
  const xdk::OriginalGuestBufferBytes* ib=nullptr;
  if(indexed) {
    if(!draw.source_ib)return std::nullopt;
    ib=draw.source_ib.get();
    if(!ib->guest_object || !ib->guest_address ||
       ib->guest_object!=state.original_index_buffer_guest_object ||
       ib->guest_size!=ib->bytes.size() || ib->bytes.size()%2u ||
       uint64_t(indexed->first_index)+indexed->index_count>
           ib->bytes.size()/2u)
      return std::nullopt;
  }
  std::optional<ExactOriginalGuestMesh> unique;
  for(const auto* model:residents) {
    if(!model || !model->original_resource_hash ||
       model->original_identity.platform!=model::Platform::Xbox360)continue;
    for(const auto& mesh:model->meshes.meshes()) {
      const auto& source=mesh.original_source_vertex_bytes;
      const uint32_t stride=mesh.original_vertex_stride;
      if(!stride || !mesh.vertex_count ||
         source.size()!=uint64_t(mesh.vertex_count)*stride ||
         source.size()>vb.bytes.size() ||
         mesh.host_indices.size()!=mesh.index_count ||
         mesh.index_count>UINT32_MAX/2u ||
         (ib && mesh.index_count==0))
        continue;
      // Preserve the actual packed Xbox bytes. Decoded float3 geometry is
      // NOT a valid substitute for the XDK SetStreamSource input.
      const auto vb_matcher=std::boyer_moore_horspool_searcher(
          source.begin(),source.end());
      auto vb_from=vb.bytes.begin();
      while(vb_from!=vb.bytes.end()) {
        const auto v=std::search(vb_from,vb.bytes.end(),vb_matcher);
        if(v==vb.bytes.end())break;
        vb_from=v+1;
        const auto byte_offset=uint64_t(v-vb.bytes.begin());
        if(byte_offset%stride)continue;
        const uint64_t origin=byte_offset/stride;
        if(origin>UINT32_MAX ||
           origin+mesh.vertex_count>vb.bytes.size()/stride)
          continue;
        if(plain) {
          if(plain->start_vertex<origin ||
             uint64_t(plain->start_vertex)+plain->vertex_count>
                 origin+mesh.vertex_count)
            continue;
          if(unique)return std::nullopt;
          unique=ExactOriginalGuestMesh{
              model->original_resource_hash,mesh.original_header_offset,
              false,static_cast<uint32_t>(origin),0,0};
          continue;
        }
        // The Xbox source's original full index window may start after
        // preceding mesh indices in the SAME bound XDK index allocation.
        // Build only the expected 16-bit guest payload from actual host
        // decoded source indices, then locate exact contiguous matches.
        // Original XDK index resources can be 16-bit OR 32-bit. Only
        // full byte-exact source stream identity can authorize the width.
        for(uint32_t index_bytes:{2u,4u}) {
          // first_index and index_count are ELEMENT counts, not byte offsets.
          // The 16-bit initial screen does not bound a D3DFMT_INDEX32 Draw:
          // reading k*4 below could otherwise escape the captured allocation.
          if(ib->bytes.size()%index_bytes ||
             mesh.index_count>UINT32_MAX/index_bytes ||
             uint64_t(indexed->first_index)+indexed->index_count >
                 ib->bytes.size()/index_bytes)
            continue;
          for(bool big:{true,false}) {
            std::vector<std::byte> encoded;
            encoded.reserve(mesh.index_count*index_bytes);
            bool valid=true;
            for(const uint32_t index:mesh.host_indices) {
              if(index_bytes==2u && index>UINT16_MAX) {
                valid=false;break;
              }
              for(uint32_t byte=0;byte<index_bytes;++byte) {
                const uint32_t shift=8u*(big ?
                    (index_bytes-1u-byte):byte);
                encoded.push_back(std::byte((index>>shift)&0xffu));
              }
            }
            if(!valid || encoded.empty() || encoded.size()>ib->bytes.size())
              continue;
            const auto index_matcher=std::boyer_moore_horspool_searcher(
                encoded.begin(),encoded.end());
            auto ib_from=ib->bytes.begin();
            while(ib_from!=ib->bytes.end()) {
              const auto found=std::search(ib_from,ib->bytes.end(),index_matcher);
              if(found==ib->bytes.end())break;
              ib_from=found+1;
              const uint64_t byte_index=uint64_t(found-ib->bytes.begin());
              if(byte_index%index_bytes)continue;
              const uint64_t first=byte_index/index_bytes;
              if(first>UINT32_MAX ||
                 indexed->first_index<first ||
                 uint64_t(indexed->first_index)+indexed->index_count>
                     first+mesh.index_count)
                continue;
              bool window_valid=true;
              const uint64_t begin=uint64_t(indexed->first_index);
              const uint64_t finish=begin+indexed->index_count;
              for(uint64_t k=begin;k<finish;++k) {
                uint32_t index=0;
                for(uint32_t byte=0;byte<index_bytes;++byte) {
                  const uint32_t value=std::to_integer<uint8_t>(
                      ib->bytes[k*index_bytes+byte]);
                  const uint32_t shift=8u*(big?
                      index_bytes-1u-byte:byte);
                  index|=value<<shift;
                }
                const int64_t guest_vertex=
                    int64_t(index)+indexed->base_vertex;
                if(guest_vertex<int64_t(origin) ||
                   guest_vertex>=int64_t(origin+mesh.vertex_count)) {
                  window_valid=false;break;
                }
              }
              if(!window_valid)continue;
              if(unique) {
                // A byte-palindromic original index stream (e.g. a mesh
                // with a degenerate zero-index triangle) can decode to
                // IDENTICAL vertex numbers in both byte orders. These
                // two interpretations are one physical guest mesh window,
                // not two competing engine owners. Preserve first (Xbox
                // BE) match. Still reject any different model, source
                // window, index width or a duplicate resident witness.
                if(unique->model_hash==model->original_resource_hash &&
                   unique->mesh_header_offset==mesh.original_header_offset &&
                   unique->guest_first_vertex==origin &&
                   unique->guest_first_index==first &&
                   unique->original_index_element_bytes==index_bytes &&
                   unique->original_index_big_endian!=big)
                  continue;
                return std::nullopt;
              }
              unique=ExactOriginalGuestMesh{
                  model->original_resource_hash,mesh.original_header_offset,
                  big,static_cast<uint32_t>(origin),
                  static_cast<uint32_t>(first),
                  static_cast<uint8_t>(index_bytes)};
            }
          }
        }
      }
    }
  }
  return unique;
}

void ValidateLiveOriginalShaderPasses(
    const xdk::Frame& frame,
    std::span<const VerifiedDrawSource> sources,
    const aot::EffectCatalog& effects) {
  if(!frame.sequence || frame.calls.empty() ||
     frame.calls.size()!=sources.size())
    throw std::invalid_argument(
        "Actual original EoR XDK shader frame has missing Draws");
  std::map<uint32_t,aot::Digest> observed_vs,observed_ps;
  for(size_t i=0;i<sources.size();++i) {
    const auto& source=sources[i];
    const auto& draw=frame.calls[i];
    if(!draw.observed_vs_matrices ||
       source.original_frame_sequence!=frame.sequence ||
       source.original_call_ordinal!=i)
      throw std::invalid_argument(
          "Original shader source not bound to exact current guest Draw");
    const auto& state=*draw.observed_vs_matrices;
    if(!state.original_vs_guest_object || !state.original_ps_guest_object)
      throw std::invalid_argument(
          "Live original Xbox shader object identity missing");
    const auto real_pass=effects.FindOriginalPass(
        source.vertex_source_sha256,source.pixel_source_sha256);
    if(!real_pass ||
       real_pass->effect_hash!=source.original_effect_hash ||
       real_pass->pass_index!=source.original_pass_index)
      throw std::invalid_argument(
          "Original runtime VS/PS pair does not identify asserted EoR effect/pass");
    const auto [vs,created_vs]=observed_vs.emplace(
        state.original_vs_guest_object,source.vertex_source_sha256);
    if(!created_vs && vs->second!=source.vertex_source_sha256)
      throw std::invalid_argument(
          "Same live XDK VS guest object claimed two original source shaders");
    const auto [ps,created_ps]=observed_ps.emplace(
        state.original_ps_guest_object,source.pixel_source_sha256);
    if(!created_ps && ps->second!=source.pixel_source_sha256)
      throw std::invalid_argument(
          "Same live XDK PS guest object claimed two original source shaders");
  }
}
std::optional<uint32_t> IdentifyOriginalMaterialSurface(
    const model::UploadedOriginalModel& model,
    std::span<const gpu::VerifiedEorSampler> samplers) noexcept {
  if(!model.original_resource_hash || samplers.empty())
    return std::nullopt;
  for(const auto& sampler:samplers)
    if(sampler.original_model_hash!=model.original_resource_hash ||
       !sampler.original_texture_hash ||
       !sampler.original_material_ownership_verified ||
       !sampler.original_shader_fetch_slot_verified ||
       !sampler.original_texture_format_and_mips_verified)
      return std::nullopt;
  std::optional<uint32_t> unique;
  for(const auto& surface:model.original_surface_candidates) {
    bool all_found=true;
    for(const auto& sampler:samplers) {
      bool found=false;
      for(const auto& indexed:surface.indexed_texture_references)
        if(indexed.original_texture_hash==sampler.original_texture_hash &&
           indexed.original_resource.hash==sampler.original_texture_hash &&
           indexed.original_resource.size && !indexed.original_resource.reserved) {
          found=true;break;
        }
      if(!found) {all_found=false;break;}
    }
    if(!all_found)continue;
    if(unique)return std::nullopt;
    unique=surface.ordinal;
  }
  return unique;
}
std::vector<aot::BoundIndexedDraw> BindVerifiedGeometryDraws(
    std::span<const VerifiedDrawSource> sources,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::EffectCatalog& effects) {
  if (sources.empty() || residents.empty())
    throw std::invalid_argument("No verified original EoR geometry draw batch");
  // Reject ambiguous resource identities *before* binding any draw.
  for (size_t i=0; i<residents.size(); ++i) {
    if (!residents[i] || !residents[i]->original_resource_hash)
      throw std::invalid_argument("Null/unidentified EoR resident model");
    for (size_t j=0; j<i; ++j)
      if (residents[i]->original_resource_hash == residents[j]->original_resource_hash)
        throw std::invalid_argument("Ambiguous duplicate EoR resident model hash");
  }
  std::vector<aot::BoundIndexedDraw> out;
  out.reserve(sources.size());
  for (const auto& src : sources) {
    if (!src.engine_draw_verified || !src.mesh_material_verified ||
        !src.host_vertex_layout_verified || !src.render_state_verified ||
        !src.original_model_hash || !src.original_effect_hash ||
        !src.vertex_shader_id || !src.pixel_shader_id ||
        src.original_surface_name.empty() ||
        src.original_surface_ordinal == UINT32_MAX ||
        src.original_pass_index == UINT32_MAX ||
        !src.index_count || (src.index_count % 3u))
      throw std::invalid_argument("Original EoR Draw/material/vertex/pass identity unverified");
    const model::UploadedOriginalModel* model = nullptr;
    for (const auto* resident : residents)
      if (resident->original_resource_hash == src.original_model_hash) {
        model = resident;
        break;
      }
    if (!model || model->original_identity.platform != model::Platform::Xbox360 ||
        src.original_surface_ordinal >= model->original_surface_candidates.size())
      throw std::invalid_argument("Original EoR model/surface is not resident");
    const auto& surface = model->original_surface_candidates[src.original_surface_ordinal];
    if (surface.ordinal != src.original_surface_ordinal ||
        surface.original_material_name != src.original_surface_name)
      throw std::invalid_argument("Original EoR model material ordinal/name mismatch");
    if (!effects.HasPair(src.original_effect_hash,src.original_pass_index,
                         src.vertex_source_sha256,src.pixel_source_sha256))
      throw std::invalid_argument("Unpaired original EoR VS/PS material technique");
    const model::UploadedNativeMesh* mesh = nullptr;
    for (const auto& candidate : model->meshes.meshes()) {
      if (candidate.original_header_offset != src.original_mesh_header_offset) continue;
      if (mesh) throw std::invalid_argument("Ambiguous original EoR mesh header");
      mesh = &candidate;
    }
    if (!mesh || !mesh->position_buffer || !mesh->index_buffer ||
        !mesh->original_vertex_stream_buffer || mesh->vertex_count < 3 ||
        mesh->original_vertex_stride < 12 || mesh->original_vertex_stride > 192 ||
        uint64_t(src.first_index)+src.index_count > mesh->index_count ||
        mesh->host_indices.size()!=mesh->index_count)
      throw std::invalid_argument("Unmatched or out-of-bounds EoR indexed GPU geometry");
    // Validate the *indexed subrange* for this Draw. A single source mesh
    // can contain multiple independently rebased objects. Checking its
    // global minimum/maximum incorrectly rejects valid negative bases
    // and creates missing geometry; checking no bounds risks corrupt draws.
    const auto first=mesh->host_indices.begin()+src.first_index;
    const auto range=std::minmax_element(first,first+src.index_count);
    if(int64_t(*range.first)+src.host_base_vertex < 0 ||
       int64_t(*range.second)+src.host_base_vertex >= mesh->vertex_count)
      throw std::invalid_argument("Source-proven EoR Draw index window exceeds host vertices");
    gpu::ValidateOriginalEorSamplers(src.original_sampler_slots);
    const auto archive_material_surface=IdentifyOriginalMaterialSurface(
        *model,src.original_sampler_slots);
    if(archive_material_surface &&
       *archive_material_surface!=src.original_surface_ordinal)
      throw std::invalid_argument(
          "Original static material textures disprove live EoR surface owner");
    for (const auto& sampler:src.original_sampler_slots)
      if (sampler.original_model_hash!=src.original_model_hash ||
          sampler.original_surface_ordinal!=src.original_surface_ordinal ||
          sampler.original_effect_hash!=src.original_effect_hash ||
          sampler.original_pass_index!=src.original_pass_index)
        throw std::invalid_argument("Original EoR surface/PSO texture slot owner mismatch");
    aot::BoundIndexedDraw item;
    item.draw = {mesh->position_buffer,mesh->index_buffer,
                 src.vertex_shader_id,src.pixel_shader_id,
                 src.index_count,src.first_index,src.host_base_vertex,
                 src.original_frame_sequence,src.original_call_ordinal,
                 src.original_vs_matrices,src.original_shader_constants};
    item.vertex_source_sha256 = src.vertex_source_sha256;
    item.pixel_source_sha256 = src.pixel_source_sha256;
    item.effect_hash = src.original_effect_hash;
    item.pass_index = src.original_pass_index;
    item.original_samplers = src.original_sampler_slots;
    item.original_model_hash = src.original_model_hash;
    item.original_surface_ordinal = src.original_surface_ordinal;
    // Preserve the authenticated source mesh even when other surfaces
    // in this same model reuse its material effect and shader pair.
    item.original_mesh_header_offset = src.original_mesh_header_offset;
    item.source_nonindexed_linearization_verified =
        src.original_linear_draw_verified;
    out.push_back(std::move(item));
  }
  return out;
}
std::vector<VerifiedDrawSource> MatchCapturedXdkDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkIndexedSubmission> verified) {
  if(!frame.sequence || frame.dropped || frame.dropped_commands ||
     (!frame.commands.empty() &&
      !xdk::ValidateOriginalCommandTimeline(frame)) ||
     frame.calls.empty() ||
     frame.calls.size()!=verified.size())
    throw std::invalid_argument("Original Xbox XDK frame has missing/unmapped Draws");
  std::vector<VerifiedDrawSource> sources;
  sources.reserve(frame.calls.size());
  for(size_t i=0;i<frame.calls.size();++i) {
    const auto actual=xdk::DecodeOriginalIndexedXdkCall(frame.calls[i]);
    const auto& proven=verified[i];
    const auto& expected=proven.original_guest_call;
    const auto& original=proven.native_draw;
    if(!actual || actual->primitive!=4u ||
       actual->index_count%3u || !proven.original_triangle_list_verified ||
       !proven.guest_to_native_index_stream_verified ||
       !proven.original_material_at_draw_verified ||
       expected.xdk_function!=actual->xdk_function ||
       expected.caller_lr!=actual->caller_lr ||
       expected.guest_device!=actual->guest_device ||
       expected.primitive!=actual->primitive ||
       expected.base_vertex!=actual->base_vertex ||
       expected.first_index!=actual->first_index ||
       expected.index_count!=actual->index_count ||
       original.index_count!=actual->index_count ||
       !original.engine_draw_verified || !original.mesh_material_verified ||
       !original.host_vertex_layout_verified || !original.render_state_verified)
      throw std::invalid_argument("Guest Xbox indexed Draw lacks exact source-proven EoR owner");
    // The original Xbox XDK index offset/baseVertex are from guest streams.
    // The host mesh has its OWN rebased indexed geometry and vertex origin.
    auto tagged=original;
    tagged.original_linear_draw_verified=false; // indexed source method
    tagged.original_frame_sequence=frame.sequence;
    tagged.original_call_ordinal=static_cast<uint32_t>(i);
    tagged.original_vs_matrices=frame.calls[i].observed_vs_matrices;
    tagged.original_shader_constants=frame.calls[i].observed_shader_constants;
    sources.push_back(std::move(tagged));
  }
  return sources;
}
OriginalXdkResourceIdentity ResourcesAtDraw(
    const xdk::OriginalVsMatrices& state) noexcept {
  return {state.original_guest_device,
          state.original_vertex_declaration_guest_object,
          state.original_index_buffer_guest_object,
          state.original_vs_guest_object,
          state.original_ps_guest_object,
          state.original_rt0_guest_object,
          state.original_depth_guest_object,
          state.original_texture_guest_objects,
          state.original_vertex_buffer_guest_objects};
}
namespace {
bool SameOriginalResources(
    const OriginalXdkResourceIdentity& expected,
    const OriginalXdkResourceIdentity& observed,
    const VerifiedXdkResourceWitness& evidence,
    bool indexed) noexcept {
  if(expected.device!=observed.device ||
     expected.vertex_declaration!=observed.vertex_declaration ||
     expected.vertex_shader!=observed.vertex_shader ||
     expected.pixel_shader!=observed.pixel_shader ||
     expected.rt0!=observed.rt0 ||
     expected.depth!=observed.depth ||
     (indexed && (!expected.index_buffer || !observed.index_buffer ||
                  expected.index_buffer!=observed.index_buffer)))
    return false;
  const auto* original=std::get_if<VerifiedXdkIndexedSubmission>(
      &evidence.original_source_submission);
  const auto& native_draw=original?original->native_draw:
      std::get<VerifiedXdkVerticesSubmission>(
          evidence.original_source_submission).native_draw;
  // An effect/material does not identify a resident mesh: enforce actual
  // source-proven XDK vertex-stream bindings BEFORE selecting its buffers.
  if(!evidence.original_xdk_vertex_stream_slot_mapping_verified ||
     !evidence.source_verified_vertex_stream_slot_mask)
    return false;
  for(uint32_t slot=0;slot<16;++slot) {
    if(!(evidence.source_verified_vertex_stream_slot_mask &
         (uint16_t(1u)<<slot)))continue;
    if(!expected.vertex_buffers[slot] ||
       expected.vertex_buffers[slot]!=observed.vertex_buffers[slot])
      return false;
  }
  if(!native_draw.original_sampler_slots.empty() &&
     (!evidence.original_xdk_texture_slot_mapping_verified ||
      !evidence.source_verified_texture_slot_mask))
    return false;
  // No texture-bearing material may be identified from an unproven XDK
  // texture slot mask; never silently resolve a sampler using nearby hashes.
  if(evidence.source_verified_texture_slot_mask &&
     !evidence.original_xdk_texture_slot_mapping_verified)
    return false;
  for(uint32_t slot=0;slot<16;++slot) {
    if(!(evidence.source_verified_texture_slot_mask & (uint16_t(1u)<<slot)))
      continue;
    if(!expected.textures[slot] ||
       expected.textures[slot]!=observed.textures[slot])
      return false;
  }
  return true;
}
}
std::vector<std::shared_ptr<const model::UploadedOriginalModel>>
RetainVerifiedOriginalFrameResidents(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkResourceWitness> witnesses,
    const VerifiedOriginalResidentLookup& lookup) {
  // No guest EoR model hash may be manufactured from LR, ShaderEffects or
  // PM4 state. Every requested archive hash must be vouched for by the
  // ORIGINAL source engine Draw and match this exact complete XDK frame.
  if(!frame.sequence || witnesses.empty() || !lookup ||
     !xdk::ValidateOriginalCommandTimeline(frame))
    throw std::invalid_argument("Missing verified original EoR model frame");
  std::map<uint32_t,std::shared_ptr<const model::UploadedOriginalModel>> owned;
  for(const auto& witness:witnesses) {
    const auto& source=std::visit([](const auto& link)
        -> const VerifiedDrawSource& {return link.native_draw;},
        witness.original_source_submission);
    if(!witness.original_guest_resource_to_material_mapping_verified ||
       !source.original_frame_sequence ||
       source.original_frame_sequence!=frame.sequence ||
       !source.engine_draw_verified || !source.mesh_material_verified ||
       !source.host_vertex_layout_verified || !source.render_state_verified ||
       !source.original_model_hash || source.original_surface_name.empty() ||
       source.original_surface_ordinal==UINT32_MAX ||
       source.original_mesh_header_offset==SIZE_MAX)
      throw std::invalid_argument("Original EoR Draw owner unverified");
    auto it=owned.find(source.original_model_hash);
    std::shared_ptr<const model::UploadedOriginalModel> resident=
        it==owned.end() ? lookup(source.original_model_hash) : it->second;
    if(!resident || resident->original_resource_hash!=source.original_model_hash ||
       resident->original_identity.platform!=model::Platform::Xbox360 ||
       source.original_surface_ordinal>=resident->original_surface_candidates.size() ||
       resident->original_surface_candidates[source.original_surface_ordinal].
           original_material_name!=source.original_surface_name)
      throw std::invalid_argument("Missing original EoR GPU model or surface");
    // A common material effect is not proof of a particular original mesh.
    // This checks that the host buffers really belong to its source header.
    unsigned matching_meshes=0;
    for(const auto& mesh:resident->meshes.meshes())
      if(mesh.original_header_offset==source.original_mesh_header_offset &&
         mesh.position_buffer && mesh.index_buffer &&
         mesh.vertex_count>=3 && mesh.index_count>=3)
        ++matching_meshes;
    if(matching_meshes!=1)
      throw std::invalid_argument("Original EoR mesh ownership ambiguous");
    if(it==owned.end())
      owned.emplace(source.original_model_hash,std::move(resident));
  }
  std::vector<std::shared_ptr<const model::UploadedOriginalModel>> result;
  result.reserve(owned.size());
  std::vector<const model::UploadedOriginalModel*> borrowed;
  borrowed.reserve(owned.size());
  for(const auto& [_,model]:owned) {
    borrowed.push_back(model.get());
    result.push_back(model);
  }
  // Enforce exact XDK guest object/window/texture/vertex-stream identity
  // and a one-to-one source ownership for every captured Draw BEFORE
  // publishing any model reference to the Vulkan worker.
  const auto matched=ResolveCapturedResourceDraws(frame,witnesses,borrowed);
  if(matched.size()!=frame.calls.size())
    throw std::invalid_argument("Original EoR frame has unmatched Draws");
  return result;
}
// The original shader class scope is sourced from the same PPC thread as
// the XDK Draw, before the game updates that renderer's scene/material state.
// This stage enriches *independently supplied* original material witnesses;
// it cannot discover or assert an EoR original model/material/effect/pass.
namespace {
bool SameOriginalEorShaderMethod(
    const xdk::OriginalEorShaderMethodAtDraw& a,
    const xdk::OriginalEorShaderMethodAtDraw& b) noexcept {
  return a.original_shader_method_guest==b.original_shader_method_guest &&
         a.original_shader_instance_guest==b.original_shader_instance_guest &&
         a.original_shader_vtable_guest==b.original_shader_vtable_guest &&
         a.source_r4_through_r7==b.source_r4_through_r7 &&
         a.nesting_depth==b.nesting_depth &&
         a.original_shader_method_vtable_verified==
             b.original_shader_method_vtable_verified &&
         a.original_decal_render_item_guest==b.original_decal_render_item_guest &&
         a.original_decal_geometry_guest==b.original_decal_geometry_guest &&
         a.original_decal_xdk_vb_ib_binding_verified==
             b.original_decal_xdk_vb_ib_binding_verified &&
         a.original_decal_material_descriptor_guest==
             b.original_decal_material_descriptor_guest &&
         a.original_decal_material_context_guest==
             b.original_decal_material_context_guest &&
         a.original_decal_material_word0==b.original_decal_material_word0 &&
         a.original_decal_material_word8==b.original_decal_material_word8 &&
         a.original_decal_item_material_binding_verified==
             b.original_decal_item_material_binding_verified;
}
bool ValidOriginalEorShaderMethod(
    const xdk::OriginalEorShaderMethodAtDraw& source) noexcept {
  return source.original_shader_method_vtable_verified &&
         source.original_shader_instance_guest &&
         source.original_shader_vtable_guest &&
         xdk::ProvenUsaEorShaderMethodVtablePair(
             source.original_shader_method_guest,
             source.original_shader_vtable_guest) &&
         source.nesting_depth && source.nesting_depth<=16 &&
         xdk::ProvenUsaEorShaderMethodVtablePair(
             source.original_shader_method_guest,
             source.original_shader_vtable_guest);
}
}
std::vector<VerifiedXdkResourceWitness> AttachLiveEorShaderMethodParents(
    const xdk::Frame& exact_original,
    std::span<const VerifiedXdkResourceWitness> material_owner_witnesses) {
  if(!exact_original.sequence || exact_original.dropped ||
     exact_original.dropped_commands || exact_original.calls.empty() ||
     material_owner_witnesses.empty() ||
     (!exact_original.commands.empty() &&
      !xdk::ValidateOriginalCommandTimeline(exact_original)))
    throw std::invalid_argument(
        "Missing original PPC Present or independently verified material witnesses");
  std::vector<VerifiedXdkResourceWitness> attached(
      material_owner_witnesses.begin(),material_owner_witnesses.end());
  for(auto& witness:attached) {
    const auto& source=std::visit(
        [](const auto& s)->const VerifiedDrawSource&{
          return s.native_draw;
        },witness.original_source_submission);
    if(source.original_frame_sequence!=exact_original.sequence ||
       source.original_call_ordinal>=exact_original.calls.size())
      throw std::invalid_argument(
          "Stale original engine shader method vs Xbox Draw sequence");
    const auto& original_call=exact_original.calls[source.original_call_ordinal];
    if(!xdk::ValidOriginalDecalGeometryBinding(original_call))
      throw std::invalid_argument("Original Decal engine GPU geometry provenance invalid");
    const auto& observed=original_call.original_engine_shader_method;
    if(!observed) {
      if(witness.original_source_engine_shader_method)
        throw std::invalid_argument("An engine shader parent was invented");
      continue;
    }
    if(!ValidOriginalEorShaderMethod(*observed) ||
       (witness.original_source_engine_shader_method &&
        !SameOriginalEorShaderMethod(
            *witness.original_source_engine_shader_method,*observed)))
      throw std::invalid_argument(
          "Original EoR shader entry/vtable/instance source mismatch");
    witness.original_source_engine_shader_method=*observed;
  }
  return attached;
}

std::vector<VerifiedDrawSource> ResolveCapturedResourceDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkResourceWitness> witnesses,
    std::span<const model::UploadedOriginalModel* const> residents) {
  if(!frame.sequence || frame.dropped || frame.dropped_commands ||
     (!frame.commands.empty() &&
      !xdk::ValidateOriginalCommandTimeline(frame)) ||
     frame.calls.empty() ||
     frame.calls.size()>xdk::FrameCapture::kMaxDraws || witnesses.empty())
    throw std::invalid_argument("Incomplete original Xbox resource-witness Draw frame");
  std::vector<VerifiedXdkDrawSubmission> matched;
  matched.reserve(frame.calls.size());
  // Compute the actual byte-exact guest VB+IB owner ONCE per source Draw,
  // before choosing among multiple possible original EoR material witnesses.
  // This is not a source of surface/pass/PSO authority by itself.
  std::vector<std::optional<ExactOriginalGuestMesh>> exact_mesh_at_draw;
  exact_mesh_at_draw.reserve(frame.calls.size());
  // Several thousand guest Draws often refer to a handful of unchanged
  // original EoR mesh/index byte allocations. The capture cache already
  // checks EVERY byte before sharing its immutable source pointers.
  // Memoize the expensive archive-wide original mesh search only for a
  // *specific* immutable guest VB/IB generation and primitive index range.
  // Changing GPU buffer bytes or baseVertex/firstIndex invalidates the key.
  using OriginalGeometryKey=std::tuple<
      uintptr_t,uintptr_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,
      uint32_t,uint32_t,uint8_t>;
  std::map<OriginalGeometryKey,
           std::optional<ExactOriginalGuestMesh>> exact_source_cache;
  for(const auto& call:frame.calls) {
    if(!call.observed_vs_matrices)
      throw std::invalid_argument("Original XDK Draw lacks actual guest shader state");
    const auto resources=ResourcesAtDraw(*call.observed_vs_matrices);
    if(!resources.device || resources.device!=call.gpr[0] ||
       !resources.vertex_shader || !resources.pixel_shader ||
       !resources.vertex_declaration)
      throw std::invalid_argument("Original Xbox Draw has incomplete resource ownership");
    const auto indexed=xdk::DecodeOriginalIndexedXdkCall(call);
    const auto vertices=xdk::DecodeOriginalVerticesXdkCall(call);
    if(!indexed && !vertices)
      throw std::invalid_argument("Unverified original Xbox primitive/Draw method");
    std::optional<ExactOriginalGuestMesh> exact;
    if(call.source_vb0 && (!indexed || call.source_ib)) {
      const OriginalGeometryKey key{
          reinterpret_cast<uintptr_t>(call.source_vb0.get()),
          reinterpret_cast<uintptr_t>(call.source_ib.get()),
          resources.vertex_buffers[0],resources.index_buffer,
          call.gpr[1],call.gpr[2],call.gpr[3],call.gpr[4],
          call.function,static_cast<uint8_t>(call.kind)};
      auto [found,inserted]=exact_source_cache.try_emplace(key);
      if(inserted)
        found->second=IdentifyExactOriginalGuestMesh(call,residents);
      exact=found->second;
      // When BOTH original indexed allocations have been captured in full,
      // their bytes are stronger evidence than a reusable shader/material
      // witness. Do not silently discard a negative exact source match and
      // send a wrong model to Vulkan merely because one witness remains.
      if(indexed && !exact)
        throw std::invalid_argument(
            "Original indexed Xbox vertex/index bytes disprove EoR mesh ownership");
    }
    exact_mesh_at_draw.push_back(exact);
    std::optional<VerifiedXdkDrawSubmission> unique;
    for(const auto& candidate:witnesses) {
      if(!candidate.original_guest_resource_to_material_mapping_verified ||
         !SameOriginalResources(candidate.original_resources,resources,
                                candidate,indexed.has_value()))
        continue;
      // The original engine shader method must belong to THIS XDK Draw,
      // with matching current guest instance/vtable/call arguments. A
      // previously verified effect, mesh or XDK shader object is NOT
      // permission to attach it to another EoR shader class invocation.
      // Strong original engine Decal r28 source item/geometry must
      // agree with the real GPU VB/IB descriptors at THIS guest Draw.
      if(!xdk::ValidOriginalDecalGeometryBinding(call))continue;
      const auto& live_engine=call.original_engine_shader_method;
      const auto& proven_engine=
          candidate.original_source_engine_shader_method;
      if(live_engine) {
        if(!ValidOriginalEorShaderMethod(*live_engine) ||
           !proven_engine ||
           !SameOriginalEorShaderMethod(*live_engine,*proven_engine))
          continue;
      } else if(proven_engine) {
        continue; // stale or fabricated engine source method owner
      }
      // If the original EoR engine independently authenticated a nonzero
      // vertex declaration slot, its CURRENT XDK allocation must actually
      // exist in this Draw snapshot and have the same verified object ID.
      // Additional position/normal/TBN streams may no longer be silently
      // presumed equal to the slot-0 source buffer.
      bool source_streams_present=true;
      for(uint32_t slot=1;slot<16;++slot) {
        if(!(candidate.source_verified_vertex_stream_slot_mask &
              (uint16_t(1u)<<slot)))continue;
        const auto& bytes=call.source_vertex_streams[slot];
        if(!bytes || !bytes->guest_object ||
           bytes->guest_object!=resources.vertex_buffers[slot] ||
           bytes->guest_size!=bytes->bytes.size() ||
           bytes->bytes.empty()) {
          source_streams_present=false;break;
        }
      }
      if(!source_streams_present)continue;
      // Multiple source-proven EoR surfaces may share an XDK shader object
      // and texture slots. A byte-exact uploaded original mesh eliminates
      // the WRONG model or mesh rather than rejecting an otherwise uniquely
      // recoverable real engine Draw as an ambiguous witness.
      const auto& candidate_source=std::visit(
          [](const auto& w)->const VerifiedDrawSource&{
            return w.native_draw;
          },candidate.original_source_submission);
      if(exact &&
         (candidate_source.original_model_hash!=exact->model_hash ||
          candidate_source.original_mesh_header_offset!=
              exact->mesh_header_offset))
        continue;
      std::optional<VerifiedXdkDrawSubmission> translated;
      if(indexed) {
        if(const auto* link=std::get_if<VerifiedXdkIndexedSubmission>(
               &candidate.original_source_submission)) {
          const auto& x=link->original_guest_call;
          if(x.xdk_function!=indexed->xdk_function ||
             x.caller_lr!=indexed->caller_lr ||
             x.guest_device!=indexed->guest_device ||
             x.primitive!=indexed->primitive ||
             indexed->primitive!=4u || indexed->index_count%3u)
            continue;
          const bool same_xdk_index_window=
              x.base_vertex==indexed->base_vertex &&
              x.first_index==indexed->first_index &&
              x.index_count==indexed->index_count;
          if(!same_xdk_index_window &&
             !candidate.original_linear_index_stream_verified)
            continue;
          auto mapped=*link;
          if(!same_xdk_index_window) {
            if(!link->guest_to_native_index_stream_verified ||
               !link->original_triangle_list_verified ||
               !link->original_material_at_draw_verified)
              continue;
            // The witness authenticates the full ORIGINAL index stream,
            // not just one Draw. Convert *current* guest indices into the
            // host decoded model's LOCAL index range without truncation.
            const int64_t host_first=
                int64_t(link->native_draw.first_index)+
                (int64_t(indexed->first_index)-int64_t(x.first_index));
            const int64_t host_base=
                int64_t(link->native_draw.host_base_vertex)+
                (int64_t(indexed->base_vertex)-int64_t(x.base_vertex));
            if(host_first<0 || host_first>UINT32_MAX ||
               host_base<INT32_MIN ||
               host_base>INT32_MAX)
              continue;
            mapped.native_draw.first_index=static_cast<uint32_t>(host_first);
            mapped.native_draw.index_count=indexed->index_count;
            mapped.native_draw.host_base_vertex=static_cast<int32_t>(host_base);
            mapped.original_guest_call=*indexed;
          }
          translated=std::move(mapped);
        }
      } else if(const auto* link=std::get_if<VerifiedXdkVerticesSubmission>(
                     &candidate.original_source_submission)) {
        const auto& x=link->original_guest_call;
        if(x.xdk_function==vertices->xdk_function &&
           x.caller_lr==vertices->caller_lr &&
           x.guest_device==vertices->guest_device &&
           x.primitive==vertices->primitive &&
           x.start_vertex==vertices->start_vertex &&
           x.vertex_count==vertices->vertex_count)
          translated=*link;
      }
      if(!translated)continue;
      if(unique)
        throw std::invalid_argument("Ambiguous original Xbox material/mesh mapping");
      unique=std::move(*translated);
    }
    if(!unique)
      throw std::invalid_argument("No verified original Xbox model/material for live Draw");
    matched.push_back(std::move(*unique));
  }
  // Revalidate every primitive, real source index/vertex window, shader
  // identity and owner through the existing mixed PPC native bridge.
  const auto resolved=MatchCapturedMixedDraws(frame,matched,residents);
  for(size_t i=0;i<frame.calls.size();++i) {
    const auto& draw=frame.calls[i];
    if(!draw.source_vb0 ||
       (xdk::DecodeOriginalIndexedXdkCall(draw) && !draw.source_ib))continue;
    const auto& exact=exact_mesh_at_draw[i];
    if(exact && (exact->model_hash!=resolved[i].original_model_hash ||
       exact->mesh_header_offset!=resolved[i].original_mesh_header_offset))
      throw std::invalid_argument(
          "Actual original XDK buffer bytes disprove engine mesh owner");
    // Additional exact source evidence must not contradict the existing
    // asserted XDK->host baseVertex and firstIndex remapping.
    if(exact) {
      const auto original=xdk::DecodeOriginalIndexedXdkCall(draw);
      if(original &&
         (original->first_index<exact->guest_first_index ||
          resolved[i].first_index!=
              original->first_index-exact->guest_first_index ||
          int64_t(resolved[i].host_base_vertex)!=
              int64_t(original->base_vertex)-exact->guest_first_vertex))
        throw std::invalid_argument(
            "Verified EoR host Draw disagrees with exact original GPU buffer offsets");
    }
  }
  return resolved;
}
void ReplayCapturedResourceScene(
    const xdk::Frame& captured,
    std::span<const VerifiedXdkResourceWitness> verified,
    const FrameGeometry& frame,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& backend,
    bool* scene_begin_attempted) {
  if(scene_begin_attempted)*scene_begin_attempted=false;
  // No sole Vulkan presenter may replay only Draws while silently
  // dropping original render-target binds, clears and resolves.
  if(!xdk::ValidateOriginalCommandTimeline(captured))
    throw std::invalid_argument("Incomplete source RT/Draw/Resolve/Present timeline");
  const auto source=ResolveCapturedResourceDraws(captured,verified,residents);
  ValidateLiveOriginalShaderPasses(captured,source,effects);
  const auto commands=BindVerifiedGeometryDraws(source,residents,effects);
  // Unlike the Draw-only legacy bridge, replay the true original
  // RT/Clear/Draw/Resolve/Present sequence through the native GPU.
  // An unknown source command rejects the FULL frame in preflight.
  aot::ReplayAotScene(frame,commands,bank,effects,reflection,backend,
                      &captured,scene_begin_attempted);
}
void ReplayDeliveredResourceScene(
    const xdk::DeliveredFrame& delivered,
    std::span<const VerifiedXdkResourceWitness> verified,
    const FrameGeometry& frame,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& backend,
    bool* scene_begin_attempted) {
  if(scene_begin_attempted)*scene_begin_attempted=false;
  // A one-slot mailbox may overwrite unsent frames at busy destruction
  // scenes. Rendering a late frame with stale per-object shader constants
  // could produce missing/doubled geometry. No Vulkan BeginScene on gaps.
  if(delivered.overwritten_since_take)
    throw std::invalid_argument(
        "Original EoR Present mailbox lost captured GPU frames");
  ReplayCapturedResourceScene(delivered.frame,verified,frame,residents,
                              bank,effects,reflection,backend,scene_begin_attempted);
}
std::vector<VerifiedDrawSource> MatchCapturedMixedDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkDrawSubmission> verified,
    std::span<const model::UploadedOriginalModel* const> residents) {
  if(!frame.sequence || frame.dropped || frame.dropped_commands ||
     (!frame.commands.empty() &&
      !xdk::ValidateOriginalCommandTimeline(frame)) ||
     frame.calls.empty() ||
     frame.calls.size()!=verified.size() ||
     frame.calls.size()>xdk::FrameCapture::kMaxDraws)
    throw std::invalid_argument("Original Xbox USA frame lacks a complete exact Draw stream");
  std::vector<VerifiedDrawSource> result;
  result.reserve(verified.size());
  for(size_t i=0;i<verified.size();++i) {
    VerifiedDrawSource source{};
    if(const auto* indexed=std::get_if<VerifiedXdkIndexedSubmission>(&verified[i])) {
      // The existing indexed path enforces the exact original XDK r3..r7
      // arguments, material identity and original shader provenance.
      const xdk::Frame single{frame.sequence,0,{frame.calls[i]}};
      const auto mapped=MatchCapturedXdkDraws(
          single,std::span<const VerifiedXdkIndexedSubmission>(indexed,1));
      source=mapped.front();
    } else {
      const auto& input=std::get<VerifiedXdkVerticesSubmission>(verified[i]);
      const auto actual=xdk::DecodeOriginalVerticesXdkCall(frame.calls[i]);
      const auto& expected=input.original_guest_call;
      source=input.native_draw;
      const auto triangle_indices=actual ?
          xdk::VerifiedTriangleListCount(actual->primitive,actual->vertex_count) :
          std::nullopt;
      if(!triangle_indices || !input.original_triangle_list_verified ||
         !input.guest_to_native_vertex_stream_verified ||
         !input.original_material_at_draw_verified ||
         actual->xdk_function!=expected.xdk_function ||
         actual->caller_lr!=expected.caller_lr ||
         actual->guest_device!=expected.guest_device ||
         actual->primitive!=expected.primitive ||
         actual->start_vertex!=expected.start_vertex ||
         actual->vertex_count!=expected.vertex_count ||
         source.index_count!=*triangle_indices ||
         uint64_t(input.host_first_vertex)+actual->vertex_count>UINT32_MAX ||
         !source.engine_draw_verified || !source.mesh_material_verified ||
         !source.host_vertex_layout_verified || !source.render_state_verified)
        throw std::invalid_argument("Non-indexed Xbox Draw has no exact triangle topology owner");
      const model::UploadedNativeMesh* mesh=nullptr;
      for(const auto* resident:residents)
        if(resident && resident->original_resource_hash==source.original_model_hash) {
          for(const auto& candidate:resident->meshes.meshes())
            if(candidate.original_header_offset==source.original_mesh_header_offset) {
              if(mesh)throw std::invalid_argument("Ambiguous original native mesh");
              mesh=&candidate;
            }
        }
      if(!mesh || mesh->host_indices.size()!=mesh->index_count ||
         uint64_t(source.first_index)+source.index_count>mesh->index_count ||
         uint64_t(input.host_first_vertex)+actual->vertex_count>mesh->vertex_count)
        throw std::invalid_argument("Native DrawVertices source index window unavailable");
      for(uint32_t j=0;j<source.index_count;++j) {
        const uint32_t position=actual->primitive==6u ?
            xdk::OriginalTriangleFanVertex(j/3u,j%3u) : j;
        if(int64_t(mesh->host_indices[source.first_index+j])+
           source.host_base_vertex!=int64_t(input.host_first_vertex)+position)
          throw std::invalid_argument(
              "Original DrawVertices host indices disagree with exact guest triangle topology");
      }
      // Only this validated non-indexed conversion may be replayed through
      // the indexed Vulkan API; a same-ordinal forged host Draw is rejected.
      source.original_linear_draw_verified=true;
    }
    source.original_frame_sequence=frame.sequence;
    source.original_call_ordinal=static_cast<uint32_t>(i);
    source.original_vs_matrices=frame.calls[i].observed_vs_matrices;
    source.original_shader_constants=frame.calls[i].observed_shader_constants;
    result.push_back(std::move(source));
  }
  return result;
}
void ReplayCapturedMixedScene(
    const xdk::Frame& captured,
    std::span<const VerifiedXdkDrawSubmission> verified,
    const FrameGeometry& frame,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& backend) {
  // A live captured Draw is never an offline/anonymous submission.
  // Preserve its exact RT/depth/Clear/Resolve/Present events all the way
  // into Diligent, or the AOT bridge correctly rejects the scene.
  if(!xdk::ValidateOriginalCommandTimeline(captured))
    throw std::invalid_argument("Incomplete captured mixed XDK GPU timeline");
  const auto draws=MatchCapturedMixedDraws(captured,verified,residents);
  const auto commands=BindVerifiedGeometryDraws(draws,residents,effects);
  aot::ReplayAotScene(frame,commands,bank,effects,reflection,backend,&captured);
}
void ReplayCapturedXdkScene(
    const xdk::Frame& captured,
    std::span<const VerifiedXdkIndexedSubmission> verified,
    const FrameGeometry& frame,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& backend) {
  // XDK read-only capture is the original command chronology. Do not
  // forward tagged live Draws via the anonymous scene helper: that path
  // cannot legally issue original clears or authorize physical Present.
  if(!xdk::ValidateOriginalCommandTimeline(captured))
    throw std::invalid_argument("Incomplete captured indexed XDK GPU timeline");
  const auto draws=MatchCapturedXdkDraws(captured,verified);
  const auto commands=BindVerifiedGeometryDraws(draws,residents,effects);
  aot::ReplayAotScene(frame,commands,bank,effects,reflection,backend,&captured);
}
void ReplayVerifiedGeometryScene(
    const FrameGeometry& frame,
    std::span<const VerifiedDrawSource> sources,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank, const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection, aot::ShaderBindingBackend& backend) {
  auto commands = BindVerifiedGeometryDraws(sources, residents, effects);
  aot::ReplayAotScene(frame, commands, bank, effects, reflection, backend);
}
} // namespace hulk::eor::native
