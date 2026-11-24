#include "eor_archive.h"
#include "eor_original_virtual_shader_identity.h"
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace hulk::eor {
namespace {
constexpr std::string_view kSourceXboxArchiveIndexSha256 =
    "46e6c6ec9f7428ba3a1737ab7ebad564c1b027b006c3e35156d2e036ab8b2820";
Index ReadPinnedUsaIndex(const std::filesystem::path& path,
                          bool allow_exact_source_archive_index) {
  // Read ONCE, then digest and parse the same original source bytes.
  // The private owner's two USA index variants agree on all original
  // Models/Textures hash->archive-offset/size records; neither is an
  // unrestricted regional/PC index fallback.
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
  const auto equals_digest=[&](std::string_view expected) {
    if(expected.size()!=2*digest.size())return false;
    for(size_t i=0;i<digest.size();++i)
      if(hex[digest[i]>>4]!=expected[2*i] ||
         hex[digest[i]&15]!=expected[2*i+1])
        return false;
    return true;
  };
  if (!equals_digest(kUsaGlobalIndexSha256) &&
      !(allow_exact_source_archive_index &&
        equals_digest(kSourceXboxArchiveIndexSha256)))
    throw std::invalid_argument("Unsupported original USA INDEX.IND digest");
  auto index=Index::Decode(std::as_bytes(std::span(bytes)));
  if(allow_exact_source_archive_index) {
    const auto models=index.groups().find("Models");
    const auto textures=index.groups().find("Textures");
    if(models==index.groups().end() || models->second.size()!=4174 ||
       textures==index.groups().end() || textures->second.size()!=6669)
      throw std::invalid_argument(
          "Original Xbox model/texture archive resource census mismatched");
  }
  return index;
}
}
Index Index::LoadPinnedUsa(const std::filesystem::path& path) {
  return ReadPinnedUsaIndex(path,false);
}
Index Index::LoadPinnedUsaModelArchive(const std::filesystem::path& path) {
  return ReadPinnedUsaIndex(path,true);
}
Index Index::LoadPinnedUsaGraphicsArchive(const std::filesystem::path& path) {
  auto index=ReadPinnedUsaIndex(path,true);
  const auto streams=index.groups().find("TexStreams");
  if(streams==index.groups().end() || streams->second.size()!=5836u)
    throw std::invalid_argument("Original Xbox texture stream census mismatched");
  return index;
}
} // namespace hulk::eor
