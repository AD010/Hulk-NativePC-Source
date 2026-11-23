// Independent EoR source shader register -> native SPIR-V float UBO packer.
// Xbox 360 XDK 6534 constant registers are ORIGINAL captured values, not
// Xenos GPU PM4 command bytes. The shader compiler's verified float bitmap
// identifies exactly which of the 256 VS/PS float4 registers the translated
// shader consumes. Matches packed register order of the source translator.
#pragma once
#include "eor_spirv_bank.h"
#include "eor_xdk_guest_constants.h"
#include "native_camera_projection.h"
#include <array>
#include <cstdint>
#include <span>
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
// Perform a true 16:10 VIEWPROJ rewrite in the NATIVE (not XEX/PM4)
// shader constant stream. Requires proved original main-perspective camera,
// shader matrix-vector convention and the original source register bitmap.
// All other (UI/shadow/sky/reflection) Draws retain original constants.
enum class FloatRegisterOrder { Packed, Original };
std::vector<float> ExpandVerifiedOriginalViewProjection(
    std::span<const float> vertex_constants,const VerifiedOriginalFloatLayout& layout,
    uint32_t first_register,double guest_aspect,uint32_t target_width,uint32_t target_height,
    const camera::SourceCameraProof& source_camera,FloatRegisterOrder order);
inline std::vector<float> ExpandVerifiedOriginalPackedViewProjection(
    std::span<const float> original_packed_vs,
    const VerifiedOriginalFloatLayout& source_shader_layout,
    uint32_t original_view_projection_c0,
    double guest_aspect,uint32_t target_width,uint32_t target_height,
    const camera::SourceCameraProof& source_camera) {
  return ExpandVerifiedOriginalViewProjection(original_packed_vs,source_shader_layout,
      original_view_projection_c0,guest_aspect,target_width,target_height,source_camera,FloatRegisterOrder::Packed);
}
} // namespace hulk::eor::aot
