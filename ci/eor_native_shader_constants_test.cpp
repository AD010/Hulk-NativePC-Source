#include "../native_port/src/eor_native_shader_constants.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace hulk::eor;
namespace {
template<class F> void Reject(F action) {
  bool rejected=false;
  try{action();}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
}
void Put(xdk::OriginalShaderRegisters& data,xdk::OriginalShaderStage stage,
         uint32_t reg,float first) {
  auto& bank=stage==xdk::OriginalShaderStage::Vertex?data.vertex:data.pixel;
  auto& mask=stage==xdk::OriginalShaderStage::Vertex?
      data.finite_vertex:data.finite_pixel;
  for(size_t c=0;c<4;++c)bank[reg*4+c]=first+float(c);
  mask[reg/64]|=uint64_t(1)<<(reg%64);
}
}
int main() {
  xdk::OriginalShaderRegisters captured;
  Put(captured,xdk::OriginalShaderStage::Vertex,7,7.f);
  Put(captured,xdk::OriginalShaderStage::Vertex,11,11.f);
  Put(captured,xdk::OriginalShaderStage::Vertex,21,21.f);
  Put(captured,xdk::OriginalShaderStage::Vertex,255,255.f);
  Put(captured,xdk::OriginalShaderStage::Pixel,0,100.f);
  Put(captured,xdk::OriginalShaderStage::Pixel,127,127.f);
  Put(captured,xdk::OriginalShaderStage::Pixel,255,355.f);
  aot::VerifiedOriginalFloatLayout vs{};
  vs.stage=aot::Stage::Vertex;
  vs.original_shader_sha256.fill(0x11);
  vs.original_register_bitmap[0]=(uint64_t(1)<<7) |
                                  (uint64_t(1)<<11) |
                                  (uint64_t(1)<<21);
  vs.original_register_bitmap[3]=uint64_t(1)<<63;
  vs.translated_ubo_float4_count=4;
  vs.original_ucode_register_map_verified=true;
  vs.translated_spirv_float_buffer_layout_verified=true;
  const auto source=vs.original_shader_sha256;
  auto packed=aot::PackOriginalXdkFloatConstants(
      captured,vs,aot::Stage::Vertex,source);
  assert(packed.size()==16 && packed[0]==7.f && packed[4]==11.f &&
         packed[8]==21.f && packed[12]==255.f && packed[15]==258.f);
  auto ps=vs;
  ps.stage=aot::Stage::Pixel;
  ps.original_shader_sha256.fill(0x22);
  ps.original_register_bitmap={1ull,1ull<<63,0ull,1ull<<63};
  ps.translated_ubo_float4_count=3;
  packed=aot::PackOriginalXdkFloatConstants(
      captured,ps,aot::Stage::Pixel,ps.original_shader_sha256);
  assert(packed.size()==12 && packed[0]==100.f &&
         packed[4]==127.f && packed[8]==355.f);
  Reject([&]{(void)aot::PackOriginalXdkFloatConstants(
      captured,ps,aot::Stage::Vertex,ps.original_shader_sha256);});
  Reject([&]{(void)aot::PackOriginalXdkFloatConstants(
      captured,vs,aot::Stage::Vertex,ps.original_shader_sha256);});
  auto invalid=vs;
  invalid.original_ucode_register_map_verified=false;
  Reject([&]{(void)aot::PackOriginalXdkFloatConstants(
      captured,invalid,aot::Stage::Vertex,source);});
  invalid=vs;
  invalid.translated_spirv_float_buffer_layout_verified=false;
  Reject([&]{(void)aot::PackOriginalXdkFloatConstants(
      captured,invalid,aot::Stage::Vertex,source);});
  invalid=vs;
  invalid.translated_ubo_float4_count=5;
  Reject([&]{(void)aot::PackOriginalXdkFloatConstants(
      captured,invalid,aot::Stage::Vertex,source);});
  auto truncated=captured;
  truncated.finite_vertex[0] &= ~(uint64_t(1)<<11);
  Reject([&]{(void)aot::PackOriginalXdkFloatConstants(
      truncated,vs,aot::Stage::Vertex,source);});
  // Pure native wide-aspect projection: correct only the verified
  // original main-view c11..c14 matrix inside its packed VS shader UBO.
  Put(captured,xdk::OriginalShaderStage::Vertex,12,12.f);
  Put(captured,xdk::OriginalShaderStage::Vertex,13,13.f);
  Put(captured,xdk::OriginalShaderStage::Vertex,14,14.f);
  auto wide=vs;
  wide.original_register_bitmap[0] |=
      (uint64_t(1)<<12)|(uint64_t(1)<<13)|(uint64_t(1)<<14);
  wide.translated_ubo_float4_count=7;
  const auto wide_values=aot::PackOriginalXdkFloatConstants(
      captured,wide,aot::Stage::Vertex,source);
  const hulk::camera::SourceCameraProof proof{
      true,true,true,hulk::camera::ClipMatrixConvention::RowVector};
  const auto expanded=aot::ExpandVerifiedOriginalPackedViewProjection(
      wide_values,wide,11,16.0/9.0,2560,1600,proof);
  // Original bitmap order: c7,c11,c12,c13,c14,c21,c255.
  assert(expanded.size()==wide_values.size() &&
         expanded[0]==7.f &&
         std::abs(expanded[4+1]-10.8f)<1e-5f &&
         std::abs(expanded[8+1]-11.7f)<1e-5f &&
         expanded[4]==11.f &&
         expanded[24]==255.f);
  const auto direct=aot::ExpandVerifiedOriginalViewProjection(captured.vertex,wide,11,
      16.0/9.0,2560,1600,proof,aot::FloatRegisterOrder::Original);
  assert(direct.size()==1024 && direct[7*4]==captured.vertex[7*4] &&
         direct[255*4]==captured.vertex[255*4] && std::abs(direct[11*4+1]-10.8f)<1e-5f);
  // 21:9 and 32:9 must widen horizontal view, NOT crop vertically.
  const auto wider=aot::ExpandVerifiedOriginalPackedViewProjection(
      wide_values,wide,11,16.0/9.0,3440,1440,proof);
  const double factor=(16.0/9.0)/(3440.0/1440.0);
  assert(wider.size()==wide_values.size() &&
         std::abs(wider[4]-float(11.0*factor))<1e-5f &&
         std::abs(wider[8]-float(12.0*factor))<1e-5f &&
         wider[5]==12.f && wider[0]==7.f && wider[24]==255.f);
  const auto doublewide=aot::ExpandVerifiedOriginalPackedViewProjection(
      wide_values,wide,11,16.0/9.0,5120,1440,proof);
  assert(std::abs(doublewide[4]-5.5f)<1e-5f &&
         std::abs(doublewide[8]-6.f)<1e-5f &&
         doublewide[5]==12.f);
  const auto portrait=aot::ExpandVerifiedOriginalPackedViewProjection(
      wide_values,wide,11,16.0/9.0,1080,1920,proof);
  assert(portrait[4]==11.f &&
         std::abs(portrait[5]-float(12.0*(1080.0/1920.0)/(16.0/9.0)))<1e-5f);
  auto no_camera=proof;
  no_camera.original_main_perspective_view=false;
  Reject([&]{(void)aot::ExpandVerifiedOriginalPackedViewProjection(
      wide_values,wide,11,16.0/9.0,2560,1600,no_camera);});
  auto missing_row=wide;
  missing_row.original_register_bitmap[0] &= ~(uint64_t(1)<<14);
  missing_row.translated_ubo_float4_count=6;
  Reject([&]{(void)aot::ExpandVerifiedOriginalPackedViewProjection(
      wide_values,missing_row,11,16.0/9.0,2560,1600,proof);});
  Reject([&]{(void)aot::ExpandVerifiedOriginalPackedViewProjection(
      wide_values,wide,11,16.0/9.0,2560,0,proof);});
  invalid=vs;
  invalid.original_register_bitmap={};
  invalid.translated_ubo_float4_count=0;
  packed=aot::PackOriginalXdkFloatConstants(
      captured,invalid,aot::Stage::Vertex,source);
  assert(packed.empty()); // shader with no float registers needs no UBO
}
