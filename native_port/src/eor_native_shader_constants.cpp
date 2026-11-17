#include "eor_native_shader_constants.h"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace hulk::eor::aot {
std::vector<float> PackOriginalXdkFloatConstants(
    const xdk::OriginalShaderRegisters& captured,
    const VerifiedOriginalFloatLayout& layout,
    Stage expected_stage,const Digest& shader_digest) {
  if((expected_stage!=Stage::Vertex && expected_stage!=Stage::Pixel) ||
     layout.stage!=expected_stage ||
     layout.original_shader_sha256!=shader_digest ||
     !std::any_of(shader_digest.begin(),shader_digest.end(),
                  [](uint8_t b){return b!=0;}) ||
     !layout.original_ucode_register_map_verified ||
     !layout.translated_spirv_float_buffer_layout_verified)
    throw std::invalid_argument(
        "Original shader float constant bitmap and translation ABI not proven");
  unsigned count=0;
  for(const uint64_t bits:layout.original_register_bitmap)
    count+=std::popcount(bits);
  if(count!=layout.translated_ubo_float4_count ||
     count>xdk::OriginalShaderRegisters::kRegisterCount)
    throw std::invalid_argument(
        "Translated SPIR-V float4 count differs from source Xenos bitmap");
  std::vector<float> packed;
  packed.reserve(size_t(count)*4);
  for(uint32_t i=0;i<xdk::OriginalShaderRegisters::kRegisterCount;++i) {
    if((layout.original_register_bitmap[i/64] &
        (uint64_t(1)<<(i%64)))==0)continue;
    const auto value=xdk::ReadOriginalShaderFloat4(
        captured,expected_stage==Stage::Vertex ?
            xdk::OriginalShaderStage::Vertex :
            xdk::OriginalShaderStage::Pixel,i);
    if(!value)
      throw std::invalid_argument(
          "Original XDK used VS/PS float4 register absent or non-finite");
    packed.insert(packed.end(),value->begin(),value->end());
  }
  if(packed.size()!=size_t(count)*4)
    throw std::logic_error("Incomplete original shader float4 packing");
  return packed;
}
std::vector<float> ExpandVerifiedOriginalPackedViewProjection(
    std::span<const float> source,
    const VerifiedOriginalFloatLayout& layout,
    uint32_t c0,double guest_aspect,uint32_t width,uint32_t height,
    const camera::SourceCameraProof& camera_proof) {
  if(layout.stage!=Stage::Vertex ||
     !layout.original_ucode_register_map_verified ||
     !layout.translated_spirv_float_buffer_layout_verified ||
     c0>252 || source.size()!=
        size_t(layout.translated_ubo_float4_count)*4)
    throw std::invalid_argument(
        "Original EoR VS ViewProj compacted register layout unverified");
  std::array<size_t,4> offsets{};
  std::array<float,16> original{};
  for(uint32_t row=0;row<4;++row) {
    const uint32_t reg=c0+row;
    const uint64_t bit=uint64_t(1)<<(reg%64);
    if(!(layout.original_register_bitmap[reg/64]&bit))
      throw std::invalid_argument("Original ViewProj source register absent");
    size_t compact=0;
    for(uint32_t group=0;group<reg/64;++group)
      compact+=std::popcount(layout.original_register_bitmap[group]);
    compact+=std::popcount(layout.original_register_bitmap[reg/64]&(bit-1));
    if(compact>=layout.translated_ubo_float4_count)
      throw std::invalid_argument("Source shader packed VS index invalid");
    offsets[row]=compact*4;
    for(uint32_t col=0;col<4;++col)
      original[row*4+col]=source[offsets[row]+col];
  }
  const auto corrected=camera::ExpandVerifiedVerticalView(
      original,guest_aspect,width,height,camera_proof);
  if(!corrected)
    throw std::invalid_argument("Original Hulk main-view projection proof absent");
  std::vector<float> out(source.begin(),source.end());
  for(uint32_t row=0;row<4;++row)
    for(uint32_t col=0;col<4;++col)
      out[offsets[row]+col]=(*corrected)[row*4+col];
  return out;
}
} // namespace hulk::eor::aot
