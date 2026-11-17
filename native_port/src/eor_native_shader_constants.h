// Independent EoR source shader register -> native SPIR-V float UBO packer.
// Xbox 360 XDK 6534 constant registers are ORIGINAL captured values, not
// Xenos GPU PM4 command bytes. The shader compiler's verified float bitmap
// identifies exactly which of the 256 VS/PS float4 registers the translated
// shader consumes. Matches packed register order of the source translator.
#pragma once
#include "eor_spirv_bank.h"
#include "eor_xdk_guest_constants.h"
#include <array>
#include <cstdint>
#include <vector>

namespace hulk::eor::aot {
struct VerifiedOriginalFloatLayout {
  Stage stage=Stage::Vertex;
  Digest original_shader_sha256{};
  // Four 64-bit words, original Xenos float4 register c0..c255.
  std::array<uint64_t,4> original_register_bitmap{};
  uint32_t translated_ubo_float4_count=0;
  bool original_ucode_register_map_verified=false;
  bool translated_spirv_float_buffer_layout_verified=false;
};
// Returns tightly packed vec4 float values, ascending c# order, without
// inventing an SPIR-V descriptor set or variable. That exact UBO variable
// name, binding and shader-stage descriptor must be proved independently.
// Used constants containing NaN/Inf fail; unused originals need not be finite.
std::vector<float> PackOriginalXdkFloatConstants(
    const xdk::OriginalShaderRegisters& captured,
    const VerifiedOriginalFloatLayout& source_shader_layout,
    Stage expected_stage,const Digest& expected_original_shader_sha256);
} // namespace hulk::eor::aot
