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
};
std::vector<SpirvImageResource> InspectSpirvImageResources(
    std::span<const uint32_t> verified_shader_spirv);
} // namespace hulk::eor::aot
