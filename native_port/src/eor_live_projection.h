#pragma once
// Native projection ownership from the original live camera register file.
// The original source CTAB names and exact SPIR-V register reads are separate
// from the matrix-shape test. No guest memory writes, FOV guessing, or stretch.
#include "native_camera_projection.h"
#include "eor_shader_reflection.h"
#include "eor_xdk_guest_constants.h"
#include <vector>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <span>
#include <string>
#include <stdexcept>

namespace hulk::eor::aot {
inline std::optional<camera::ClipMatrixConvention> IdentifyLivePerspectiveStorage(
    const std::array<float,16>& matrix,double aspect) noexcept {
  if(!std::isfinite(aspect) || aspect<=0)return {};
  for(float v:matrix)if(!std::isfinite(v))return {};
  const auto matches=[&](bool transposed) {
    std::array<std::array<double,3>,4> axes{};
    for(size_t r=0;r<4;++r)
      for(size_t c=0;c<3;++c)
        axes[r][c]=matrix[transposed ? c*4+r : r*4+c];
    auto dot=[](const auto& x,const auto& y){
      return x[0]*y[0]+x[1]*y[1]+x[2]*y[2];
    };
    std::array<double,4> length{};
    for(size_t r=0;r<4;++r)length[r]=std::sqrt(dot(axes[r],axes[r]));
    if(length[0]<0.05 || length[1]<0.05 || length[0]>100 ||
       length[1]>100 || std::abs(length[3]-1.0)>0.005 ||
       std::abs(length[1]/length[0]-aspect)>aspect*0.002 ||
       length[2]<0.5 || length[2]>2.0)return false;
    auto perpendicular=[&](size_t a,size_t b) {
      return std::abs(dot(axes[a],axes[b]))<=length[a]*length[b]*0.002;
    };
    // Perspective P*rigid View: X and Y are orthogonal to each other and
    // to W; the Z row is parallel to W. Orthographic W has zero spatial
    // norm and cannot satisfy this test.
    return perpendicular(0,1) && perpendicular(0,3) &&
           perpendicular(1,3) &&
           std::abs(dot(axes[2],axes[3]))>=length[2]*length[3]*0.999998;
  };
  const bool row=matches(false),column=matches(true);
  if(row==column)return {}; // ambiguous storage is not evidence
  return row ? camera::ClipMatrixConvention::ColumnVector :
               camera::ClipMatrixConvention::RowVector;
}
inline bool ScalarReadsOriginalMatrix(const Blob& shader,RegisterSpan range) {
  if(shader.stage!=Stage::Vertex || range.count!=4 ||
     range.index>252 || shader.spirv.size()<5)return false;
  std::map<uint32_t,uint32_t> constants;
  std::set<uint32_t> blocks;
  std::set<uint32_t> read_registers;
  const auto& words=shader.spirv;
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    if(!n || n>words.size()-i)return false;
    const auto* w=words.data()+i;
    if(op==5u && n>=3) {
      std::string name;
      bool end=false;
      for(size_t j=2;j<n && !end;++j)
        for(unsigned b=0;b<4;++b) {
          const auto c=static_cast<char>(w[j]>>(8*b));
          if(!c){end=true;break;}
          name+=c;
        }
      if(!end)return false;
      if(name=="HulkVertexConstants")blocks.insert(w[1]);
    } else if(op==43u && n==4u)constants.emplace(w[2],w[3]);
    i+=n;
  }
  if(blocks.size()!=1)return false;
  const auto block=*blocks.begin();
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    const auto* w=words.data()+i;
    if((op==65u || op==66u) && n==6u && w[3]==block) {
      const auto member=constants.find(w[4]),index=constants.find(w[5]);
      if(member!=constants.end() && index!=constants.end() && member->second==0)
        read_registers.insert(index->second);
    }
    i+=n;
  }
  for(uint32_t i=0;i<4;++i)if(!read_registers.contains(range.index+i))return false;
  return true;
}
struct LivePerspectiveProjection {
  RegisterSpan registers;
  camera::SourceCameraProof proof;
};
inline std::optional<LivePerspectiveProjection> IdentifyLiveScalarProjection(
    const Blob& vertex,const ReflectedShader& reflection,
    const xdk::OriginalShaderRegisters& captured,double guest_aspect) {
  if(!captured.complete_float_banks || reflection.stage!=Stage::Vertex)return {};
  std::optional<LivePerspectiveProjection> unique;
  for(const auto range:{reflection.view_projection,reflection.sky_view_projection}) {
    if(!range.Present() || !ScalarReadsOriginalMatrix(vertex,range))continue;
    std::array<float,16> matrix{};
    bool complete=true;
    for(uint32_t i=0;i<4;++i) {
      const auto reg=xdk::ReadOriginalShaderFloat4(captured,xdk::OriginalShaderStage::Vertex,range.index+i);
      if(!reg){complete=false;break;}
      std::copy(reg->begin(),reg->end(),matrix.begin()+i*4);
    }
    if(!complete)continue;
    const auto convention=IdentifyLivePerspectiveStorage(matrix,guest_aspect);
    if(!convention)continue;
    if(unique)return {};
    unique=LivePerspectiveProjection{range,{true,true,true,*convention}};
  }
  return unique;
}
inline std::vector<float> AdaptLiveDirectScalarProjection(
    const Blob& vertex,const ReflectedShader& reflection,
    const xdk::OriginalShaderRegisters& captured,double guest_aspect,
    uint32_t width,uint32_t height,uint32_t expected_first,
    camera::ClipMatrixConvention expected_convention) {
  const auto verified=IdentifyLiveScalarProjection(vertex,reflection,captured,guest_aspect);
  if(!verified || verified->registers.index!=expected_first ||
     verified->proof.convention!=expected_convention)
    throw std::invalid_argument("Current original perspective matrix no longer proves native projection");
  std::array<float,16> original{};
  std::copy_n(captured.vertex.begin()+expected_first*4,16,original.begin());
  const auto corrected=camera::AdaptVerifiedMainViewProjection(original,guest_aspect,
      width,height,verified->proof);
  if(!corrected)throw std::invalid_argument("Invalid original live projection");
  std::vector<float> result(captured.vertex.begin(),captured.vertex.end());
  std::copy(corrected->begin(),corrected->end(),result.begin()+expected_first*4);
  return result;
}
} // namespace hulk::eor::aot
