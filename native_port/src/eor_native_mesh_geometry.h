#pragma once
// Decode original Xbox 360 EoR indexed model geometry directly to host CPU buffers.
// No guest GPU packets, PM4, ReXGlue, Xenia, or original game binary redistribution.
#include "eor_model_resource.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace hulk::eor::model {
struct NativePosition { float x=0.f,y=0.f,z=0.f; };
struct NativeIndexedMesh {
 size_t source_header_offset=0,source_indices_offset=0,source_vertices_offset=0;
 uint32_t original_vertex_stride=0,original_attribute_count=0;
 std::vector<std::array<std::byte,4>> original_attribute_declarations;
 std::vector<NativePosition> positions;
 std::vector<uint32_t> triangle_list_indices;
 // Original non-position attributes retained for later native UV/normals/skinning.
 std::vector<std::byte> original_vertex_stream;
};
std::vector<NativeIndexedMesh> DecodeNativeXboxIndexedMeshes(
 std::span<const std::byte> original_model);
}
