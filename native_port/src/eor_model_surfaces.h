#pragma once
// Full original EoR model surface directory, without ReXGlue, PM4 or Vulkan.
#include "eor_model_resource.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace hulk::eor::model {
struct SurfaceHeader {
 uint32_t ordinal=0;
 std::string material_name;
 size_t name_offset=0, material_data_offset=0, header_end_offset=0;
 uint32_t attribute_a=0, attribute_b=0, material_flags=0;
};
struct SurfaceDirectory {
 Platform platform=Platform::Xbox360;
 std::string original_model_name;
 uint8_t model_kind=0;
 std::vector<SurfaceHeader> surfaces;
 size_t first_surface_offset=0, last_surface_header_end=0;
};
SurfaceDirectory ReadAllSurfaceHeaders(std::span<const std::byte> raw);
}
