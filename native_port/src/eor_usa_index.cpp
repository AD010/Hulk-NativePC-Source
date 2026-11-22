#include "eor_archive.h"
#include "eor_original_virtual_shader_identity.h"
#include <fstream>
#include <stdexcept>

namespace hulk::eor {
Index Index::LoadPinnedUsa(const std::filesystem::path& path) {
  // Parse the same bytes that authenticated the release. A second open
  // could otherwise describe a different index after a file replacement.
  std::ifstream file(path,std::ios::binary | std::ios::ate);
  constexpr std::streamoff expected_size=534176;
  if (!file || file.tellg()!=expected_size)
    throw std::runtime_error("Unexpected original USA INDEX.IND length");
  std::vector<uint8_t> bytes(expected_size);
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()),expected_size) ||
      file.peek()!=std::char_traits<char>::eof())
    throw std::runtime_error("Incomplete or changing original USA INDEX.IND");
  const auto digest=aot::SourceSha256(bytes);
  constexpr char hex[]="0123456789abcdef";
  for (size_t i=0;i<digest.size();++i) {
    if (hex[digest[i]>>4]!=kUsaGlobalIndexSha256[2*i] ||
        hex[digest[i]&15]!=kUsaGlobalIndexSha256[2*i+1])
      throw std::invalid_argument("Unsupported original USA INDEX.IND digest");
  }
  return Decode(std::as_bytes(std::span(bytes)));
}
} // namespace hulk::eor
