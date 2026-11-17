#pragma once
// Source-only SPIR-V image resource scan for preflight of original Xenos
// shader material mappings. Not a replacement for the original EoR ABI.
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace hulk::eor::aot {
struct SpirvImageResource {
  std::string exact_variable_name;
  uint32_t descriptor_set=UINT32_MAX;
  uint32_t binding=UINT32_MAX;
  // SPIR-V OpTypeImage operands: Dim=1 is 2D, Arrayed=1 requires
  // Texture2DArray even if the texture has only one array layer.
  uint32_t spirv_dim=UINT32_MAX;
  bool image_arrayed=false;
  bool image_multisampled=false;
};
std::vector<SpirvImageResource> InspectSpirvImageResources(
    std::span<const uint32_t> verified_shader_spirv);
} // namespace hulk::eor::aot
