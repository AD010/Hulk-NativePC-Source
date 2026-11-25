#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace hulk::eor::aot {
// OpName and OpExtInstImport contain a terminal LiteralString operand.
// Decode bytes explicitly; reject missing terminators and hidden suffixes.
// Do not use for OpEntryPoint, which has interface IDs after its name.
inline std::optional<std::string> DecodeTerminalSpirvString(
    std::span<const uint32_t> words) {
  std::string result;
  for(size_t i=0;i<words.size();++i) {
    const auto word=words[i];
    for(unsigned b=0;b<4;++b) {
      const auto ch=static_cast<char>((word>>(8*b))&255u);
      if(!ch) {
        if(i+1!=words.size() || (word>>(8*b))!=0)return std::nullopt;
        return result;
      }
      result.push_back(ch);
    }
  }
  return std::nullopt;
}
} // namespace hulk::eor::aot
