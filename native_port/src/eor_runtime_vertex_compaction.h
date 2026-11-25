#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace hulk::eor::gpu::live {
// Compact exact effective indices. Keep the source order of unique
// vertices stable across Draws, without decoding unused vertex holes.
// The scalar vertex ABI rejects VertexIndex builtins before calling this.
inline std::vector<uint32_t> CompactRuntimeIndices(std::vector<uint32_t>& indices) {
  if(indices.empty() || indices.size()>1048576u)
    throw std::invalid_argument("Invalid original transient index sequence");
  auto owners=indices;
  std::sort(owners.begin(),owners.end());
  owners.erase(std::unique(owners.begin(),owners.end()),owners.end());
  const uint64_t span=uint64_t(owners.back())-owners.front()+1u;
  if(span==owners.size()) {
    const auto first=owners.front();
    for(auto& i:indices)i-=first;
  } else {
    for(auto& i:indices)
      i=static_cast<uint32_t>(std::lower_bound(owners.begin(),owners.end(),i)-owners.begin());
  }
  return owners;
}
inline std::vector<std::byte> GatherRuntimeVertexBytes(
    std::span<const std::byte> allocation,uint32_t offset,uint32_t length,
    uint32_t stride,std::span<const uint32_t> owners) {
  if(!stride || stride>192u || stride%4u || owners.empty() ||
     owners.size()>1048576u || uint64_t(offset)+length>allocation.size() ||
     uint64_t(owners.size())*stride>SIZE_MAX)
    throw std::invalid_argument("Invalid original transient stream range");
  if(!std::is_sorted(owners.begin(),owners.end()) ||
     uint64_t(owners.back()+uint64_t{1})*stride>length)
    throw std::invalid_argument("Original transient vertex outside stream");
  const auto first=owners.front();
  if(uint64_t(owners.back())-first+1u==owners.size()) {
    const size_t begin=size_t(offset)+size_t(first)*stride;
    const auto range=allocation.subspan(begin,owners.size()*stride);
    return {range.begin(),range.end()};
  }
  std::vector<std::byte> output(owners.size()*stride);
  for(size_t i=0;i<owners.size();++i)
    std::copy_n(allocation.data()+size_t(offset)+size_t(owners[i])*stride,
                stride,output.data()+i*stride);
  return output;
}
// The original scalar normal/tangent ABI uses asfloat(uint4) when
// R11G11B10 packing is disabled. Preserve IEEE bits, never numeric casts.
// Blend indices are instead actual integer components (0..255).
inline std::array<uint32_t,4> RuntimeFloatAttributeBits(
    const std::array<float,4>& value) noexcept {
  return {std::bit_cast<uint32_t>(value[0]),std::bit_cast<uint32_t>(value[1]),
          std::bit_cast<uint32_t>(value[2]),std::bit_cast<uint32_t>(value[3])};
}
} // namespace hulk::eor::gpu::live
