// Standalone test exporter. The input bank is private, never committed.
// Use exactly the production gamma-lowering header, not a second converter.
#include "../src/eor_spirv_cube_gamma.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
int main(int argc,char** argv) {
  try {
    if(argc!=4)throw std::invalid_argument("input.spv mask output.spv");
    unsigned mask=0;const std::string arg=argv[2];
    const auto parsed=std::from_chars(arg.data(),arg.data()+arg.size(),mask);
    if(parsed.ec!=std::errc{} || parsed.ptr!=arg.data()+arg.size() || mask>65535)
      throw std::invalid_argument("Invalid cube slot mask");
    std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
    const auto end=file.tellg();
    if(end<20 || end>16*1024*1024 || end%4!=0)
      throw std::invalid_argument("Invalid input SPIR-V length");
    std::vector<uint32_t> words(static_cast<size_t>(end)/4);
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(words.data()),static_cast<std::streamsize>(end)))
      throw std::invalid_argument("Truncated SPIR-V");
    const auto output=hulk::eor::aot::LowerOriginalCubeGamma(words,static_cast<uint16_t>(mask));
    std::ofstream out(argv[3],std::ios::binary|std::ios::trunc);
    if(!out.write(reinterpret_cast<const char*>(output.data()),
                  static_cast<std::streamsize>(output.size()*4)))
      throw std::runtime_error("Cannot export gamma validation input");
  } catch(const std::exception& e) {
    std::cerr<<e.what()<<'\n';return 1;
  }
}
