#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace hulk::eor::aot {
// Original scalar AOT compiler emits a standalone Output float decorated
// BuiltIn PointSize. Do not treat a mere declaration or an input as a
// shader that supplies the point rasterizer with a real size.
inline bool HasOriginalPointSizeOutput(std::span<const uint32_t> words) noexcept {
  if(words.size()<5 || words[0]!=0x07230203u)return false;
  uint32_t output=0;
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    if(!n || n>words.size()-i)return false;
    if(op==71u && n==4u && words[i+2]==11u && words[i+3]==1u) {
      if(output || !words[i+1] || words[i+1]>=words[3])return false;
      output=words[i+1];
    }
    i+=n;
  }
  if(!output)return false;
  bool variable=false,store=false,vertex=false;
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    if(op==15u && n>=4u && words[i+1]==0u)vertex=true;
    if(op==59u && n>=4u && words[i+2]==output && words[i+3]==3u)variable=true;
    if(op==62u && n>=3u && words[i+1]==output)store=true;
    i+=n;
  }
  return vertex && variable && store;
}
} // namespace hulk::eor::aot
