#include "eor_native_draw_geometry.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hulk::eor::native {
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
        !src.index_count || (src.index_count % 3u) || (src.first_index % 3u))
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
                 src.original_vs_matrices};
    item.vertex_source_sha256 = src.vertex_source_sha256;
    item.pixel_source_sha256 = src.pixel_source_sha256;
    item.effect_hash = src.original_effect_hash;
    item.pass_index = src.original_pass_index;
    item.original_samplers = src.original_sampler_slots;
    item.original_model_hash = src.original_model_hash;
    item.original_surface_ordinal = src.original_surface_ordinal;
    out.push_back(std::move(item));
  }
  return out;
}
std::vector<VerifiedDrawSource> MatchCapturedXdkDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkIndexedSubmission> verified) {
  if(!frame.sequence || frame.dropped || frame.calls.empty() ||
     frame.calls.size()!=verified.size())
    throw std::invalid_argument("Original Xbox XDK frame has missing/unmapped Draws");
  std::vector<VerifiedDrawSource> sources;
  sources.reserve(frame.calls.size());
  for(size_t i=0;i<frame.calls.size();++i) {
    const auto actual=xdk::DecodeOriginalIndexedXdkCall(frame.calls[i]);
    const auto& proven=verified[i];
    const auto& expected=proven.original_guest_call;
    const auto& original=proven.native_draw;
    if(!actual || !proven.original_triangle_list_verified ||
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
    tagged.original_frame_sequence=frame.sequence;
    tagged.original_call_ordinal=static_cast<uint32_t>(i);
    tagged.original_vs_matrices=frame.calls[i].observed_vs_matrices;
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
          state.original_texture_guest_objects};
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
     (indexed && expected.index_buffer!=observed.index_buffer))
    return false;
  const auto* original=std::get_if<VerifiedXdkIndexedSubmission>(
      &evidence.original_source_submission);
  const auto& native_draw=original?original->native_draw:
      std::get<VerifiedXdkVerticesSubmission>(
          evidence.original_source_submission).native_draw;
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
std::vector<VerifiedDrawSource> ResolveCapturedResourceDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkResourceWitness> witnesses,
    std::span<const model::UploadedOriginalModel* const> residents) {
  if(!frame.sequence || frame.dropped || frame.calls.empty() ||
     frame.calls.size()>xdk::FrameCapture::kMaxDraws || witnesses.empty())
    throw std::invalid_argument("Incomplete original Xbox resource-witness Draw frame");
  std::vector<VerifiedXdkDrawSubmission> matched;
  matched.reserve(frame.calls.size());
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
    const VerifiedXdkDrawSubmission* unique=nullptr;
    for(const auto& candidate:witnesses) {
      if(!candidate.original_guest_resource_to_material_mapping_verified ||
         !SameOriginalResources(candidate.original_resources,resources,
                                candidate,indexed.has_value()))
        continue;
      bool identical=false;
      if(indexed) {
        if(const auto* link=std::get_if<VerifiedXdkIndexedSubmission>(
               &candidate.original_source_submission)) {
          const auto& x=link->original_guest_call;
          identical=x.xdk_function==indexed->xdk_function &&
                    x.caller_lr==indexed->caller_lr &&
                    x.guest_device==indexed->guest_device &&
                    x.primitive==indexed->primitive &&
                    x.base_vertex==indexed->base_vertex &&
                    x.first_index==indexed->first_index &&
                    x.index_count==indexed->index_count;
        }
      } else if(const auto* link=std::get_if<VerifiedXdkVerticesSubmission>(
                     &candidate.original_source_submission)) {
        const auto& x=link->original_guest_call;
        identical=x.xdk_function==vertices->xdk_function &&
                  x.caller_lr==vertices->caller_lr &&
                  x.guest_device==vertices->guest_device &&
                  x.primitive==vertices->primitive &&
                  x.start_vertex==vertices->start_vertex &&
                  x.vertex_count==vertices->vertex_count;
      }
      if(!identical)continue;
      if(unique)
        throw std::invalid_argument("Ambiguous original Xbox material/mesh mapping");
      unique=&candidate.original_source_submission;
    }
    if(!unique)
      throw std::invalid_argument("No verified original Xbox model/material for live Draw");
    matched.push_back(*unique);
  }
  // Revalidate every primitive, real source index/vertex window, shader
  // identity and owner through the existing mixed PPC native bridge.
  return MatchCapturedMixedDraws(frame,matched,residents);
}
void ReplayCapturedResourceScene(
    const xdk::Frame& captured,
    std::span<const VerifiedXdkResourceWitness> verified,
    const FrameGeometry& frame,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& backend) {
  const auto source=ResolveCapturedResourceDraws(captured,verified,residents);
  ReplayVerifiedGeometryScene(frame,source,residents,bank,effects,reflection,backend);
}
std::vector<VerifiedDrawSource> MatchCapturedMixedDraws(
    const xdk::Frame& frame,
    std::span<const VerifiedXdkDrawSubmission> verified,
    std::span<const model::UploadedOriginalModel* const> residents) {
  if(!frame.sequence || frame.dropped || frame.calls.empty() ||
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
      if(!actual || !input.original_triangle_list_verified ||
         !input.guest_to_native_vertex_stream_verified ||
         !input.original_material_at_draw_verified ||
         actual->xdk_function!=expected.xdk_function ||
         actual->caller_lr!=expected.caller_lr ||
         actual->guest_device!=expected.guest_device ||
         actual->primitive!=expected.primitive ||
         actual->start_vertex!=expected.start_vertex ||
         actual->vertex_count!=expected.vertex_count ||
         actual->primitive!=4u || (actual->vertex_count%3u) ||
         source.index_count!=actual->vertex_count ||
         uint64_t(input.host_first_vertex)+actual->vertex_count>UINT32_MAX ||
         !source.engine_draw_verified || !source.mesh_material_verified ||
         !source.host_vertex_layout_verified || !source.render_state_verified)
        throw std::invalid_argument("Non-indexed Xbox Draw has no exact triangle-list owner");
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
         uint64_t(input.host_first_vertex)+source.index_count>mesh->vertex_count)
        throw std::invalid_argument("Native DrawVertices source index window unavailable");
      for(uint32_t j=0;j<source.index_count;++j)
        if(int64_t(mesh->host_indices[source.first_index+j])+
           source.host_base_vertex!=int64_t(input.host_first_vertex)+j)
          throw std::invalid_argument("Original DrawVertices is not sequential host triangle-list geometry");
    }
    source.original_frame_sequence=frame.sequence;
    source.original_call_ordinal=static_cast<uint32_t>(i);
    source.original_vs_matrices=frame.calls[i].observed_vs_matrices;
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
  auto draws=MatchCapturedMixedDraws(captured,verified,residents);
  ReplayVerifiedGeometryScene(frame,draws,residents,bank,effects,reflection,backend);
}
void ReplayCapturedXdkScene(
    const xdk::Frame& captured,
    std::span<const VerifiedXdkIndexedSubmission> verified,
    const FrameGeometry& frame,
    std::span<const model::UploadedOriginalModel* const> residents,
    const aot::ShaderBank& bank,const aot::EffectCatalog& effects,
    const aot::ReflectionBank& reflection,aot::ShaderBindingBackend& backend) {
  auto draws=MatchCapturedXdkDraws(captured,verified);
  ReplayVerifiedGeometryScene(frame,draws,residents,bank,effects,reflection,backend);
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
