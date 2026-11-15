#pragma once
// Indexed Xbox/PC texture references for EVERY original EoR model surface.
#include "eor_model_surfaces.h"
#include "eor_model_material_refs.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace hulk::eor::model {
struct SurfaceMaterials {
 uint32_t ordinal=0;
 std::string original_material_name;
 uint32_t material_flags=0;
 size_t material_data_offset=0;
 std::vector<TextureReferenceCandidate> indexed_texture_references;
};
std::vector<SurfaceMaterials> AllSurfaceMaterialCandidates(
 std::span<const std::byte> original_model,const Index& matching_platform_index);
}
