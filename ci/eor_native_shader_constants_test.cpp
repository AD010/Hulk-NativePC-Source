#include "../native_port/src/eor_native_shader_constants.h"
#include <cassert>
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
  invalid=vs;
  invalid.original_register_bitmap={};
  invalid.translated_ubo_float4_count=0;
  packed=aot::PackOriginalXdkFloatConstants(
      captured,invalid,aot::Stage::Vertex,source);
  assert(packed.empty()); // shader with no float registers needs no UBO
}
