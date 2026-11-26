#include "../native_port/src/eor_native_shader_constants.h"
#include "../native_port/src/eor_live_projection.h"
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
  {
    using namespace hulk::eor::xdk;
    OriginalShaderRegisters original{};
    assert(!PackOriginalScalarBooleans(original));
    std::array<uint32_t,8> words{};
    words[0]=0xabcda55au;words[4]=0x12345678u;
    original.original_boolean_words=words;
    assert(PackOriginalScalarBooleans(original)==0x5678a55au);
    words[0]=0;words[4]=0x4001;
    original.original_boolean_words=words;
    assert(PackOriginalScalarBooleans(original)==0x40010000u);
  }
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
  // Real perspective axes, both register storage conventions; reject
  // an orthographic matrix and an unrelated aspect before altering the UBO.
  const std::array<float,16> perspective{
      1.125f,0,0,0, 0,2.f,0,0, 0,0,1.01f,-0.1f, 0,0,1.f,0};
  auto storage=aot::IdentifyLivePerspectiveStorage(perspective,16.0/9.0);
  assert(storage && *storage==hulk::camera::ClipMatrixConvention::ColumnVector);
  std::array<float,16> transposed{};
  for(size_t i=0;i<4;++i)for(size_t j=0;j<4;++j)
    transposed[i*4+j]=perspective[j*4+i];
  storage=aot::IdentifyLivePerspectiveStorage(transposed,16.0/9.0);
  assert(storage && *storage==hulk::camera::ClipMatrixConvention::RowVector);
  assert(!aot::IdentifyLivePerspectiveStorage(perspective,4.0/3.0));
  auto ortho=perspective;
  ortho[14]=0;ortho[15]=1;
  assert(!aot::IdentifyLivePerspectiveStorage(ortho,16.0/9.0));
  // Two reflected names at one register span are one camera owner.
  // Two genuinely distinct live spans must still fail closed.
  for(unsigned i=0;i<249;++i) {
    aot::Blob blob;blob.scalar_register_reads.emplace();
    xdk::OriginalShaderRegisters cap;cap.complete_float_banks=true;
    aot::ReflectedShader ref;ref.view_projection={i,4};ref.sky_view_projection={i,4};
    for(unsigned j=i;j<i+8;++j) {
      blob.scalar_register_reads->set(j);
      cap.finite_vertex[j/64]|=uint64_t{1}<<(j%64);
    }
    std::copy(perspective.begin(),perspective.end(),cap.vertex.begin()+i*4);
    const auto actual=aot::IdentifyLiveScalarProjection(blob,ref,cap,16./9.);
    assert(actual && actual->registers.index==i);
    ref.sky_view_projection={i+4,4};
    std::copy(perspective.begin(),perspective.end(),cap.vertex.begin()+(i+4)*4);
    assert(!aot::IdentifyLiveScalarProjection(blob,ref,cap,16./9.));
    cap.finite_vertex[(i+4)/64]&=~(uint64_t{1}<<((i+4)%64));
    assert(aot::IdentifyLiveScalarProjection(blob,ref,cap,16./9.));
    ref.sky_view_projection={i,4};cap.complete_float_banks=false;
    assert(!aot::IdentifyLiveScalarProjection(blob,ref,cap,16./9.));
    cap.complete_float_banks=true;blob.scalar_register_reads->reset(i);
    assert(!aot::IdentifyLiveScalarProjection(blob,ref,cap,16./9.));
  }
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
