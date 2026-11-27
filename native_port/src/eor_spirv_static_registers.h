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
  // DXC may address a float4 register or one of its components in a
  // single access chain, or split the chain at the verified member-zero
  // array pointer. Preserve the exact static register index in each form.
  std::set<uint32_t> register_arrays;
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    const auto* w=words.data()+i;
    if((op==65u || op==66u) && n>=5u && n<=7u) {
      size_t index_operand=0;
      if(w[3]==block) {
        const auto member=constants.find(w[4]);
        if(member!=constants.end() && member->second==0) {
          if(n==5u)register_arrays.insert(w[2]);
          else index_operand=5u;
        }
      } else if(register_arrays.contains(w[3]) && n<=6u) {
        index_operand=4u;
      }
      if(index_operand) {
        const auto index=constants.find(w[index_operand]);
        const auto component=index_operand+1u<n ?
            constants.find(w[index_operand+1u]) : constants.end();
        if(index!=constants.end() && index->second<256u &&
           (index_operand+1u==n ||
            (component!=constants.end() && component->second<4u)))
          read_registers.set(index->second);
      }
    }
    i+=n;
  }
  return read_registers;
}
} // namespace hulk::eor::aot
