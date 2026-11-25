#pragma once
#include "eor_spirv_literal_string.h"
#include <bitset>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>

namespace hulk::eor::aot {
inline std::bitset<256> AnalyzeScalarRegisterReads(std::span<const uint32_t> words) {
  if(words.size()<5 || words[0]!=0x07230203u)return {};
  std::map<uint32_t,uint32_t> constants;
  std::set<uint32_t> blocks;
  std::bitset<256> read_registers;
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    if(!n || n>words.size()-i)return {};
    const auto* w=words.data()+i;
    if(op==5u) {
      if(n<3)return {};
      const auto name=DecodeTerminalSpirvString(words.subspan(i+2,n-2));
      if(!name)return {};
      if(*name=="HulkVertexConstants")blocks.insert(w[1]);
    } else if(op==43u && n==4u)constants.emplace(w[2],w[3]);
    i+=n;
  }
  if(blocks.size()!=1)return {};
  const auto block=*blocks.begin();
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    const auto* w=words.data()+i;
    if((op==65u || op==66u) && n==6u && w[3]==block) {
      const auto member=constants.find(w[4]),index=constants.find(w[5]);
      if(member!=constants.end() && index!=constants.end() && member->second==0)
        if(index->second<256u)read_registers.set(index->second);
    }
    i+=n;
  }
  return read_registers;
}
} // namespace hulk::eor::aot
