#include "eor_native_draw_geometry.h"
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
        int64_t(mesh->min_index)+src.host_base_vertex < 0 ||
        int64_t(mesh->max_index)+src.host_base_vertex >= mesh->vertex_count)
      throw std::invalid_argument("Unmatched or out-of-bounds EoR indexed GPU geometry");
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
                 src.index_count,src.first_index,src.host_base_vertex};
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
