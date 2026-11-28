#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>
#include <utility>

namespace hulk::eor::source {
struct OriginalArchiveLocation {
  std::filesystem::path index, directory;
  bool operator==(const OriginalArchiveLocation&) const = default;
};
inline bool IsArchiveFile(const std::filesystem::path& file) {
  std::error_code error;
  return std::filesystem::is_regular_file(file,error) && !error;
}
// The native host can read a PC-style Data directory without a XENON
// mount. These are path candidates only: the existing pinned index and
// archive readers remain the authority for the actual resource format.
inline std::vector<OriginalArchiveLocation> OriginalArchiveLocations(const std::filesystem::path& root) {
  if(root.empty())return {};
  std::vector<OriginalArchiveLocation> result;
  const auto add=[&](const std::filesystem::path& index,const std::filesystem::path& dir){
    if(!IsArchiveFile(index))return;
    OriginalArchiveLocation value{index,dir};
    if(std::find(result.begin(),result.end(),value)==result.end())result.push_back(std::move(value));
  };
  const auto family=[&](const std::filesystem::path& base){
    const std::array dirs{base,base/"USENGLISH",base/"UKENGLISH"};
    for(const auto& dir:dirs)for(auto name:{"INDEX.IND","indexXBOX360.ind","index(1).ind","index(2).ind"}) {
      add(dir/name,dir);
      if(dir!=base)add(dir/name,base);
      else {add(dir/name,base/"USENGLISH");add(dir/name,base/"UKENGLISH");}
    }
  };
  // Prefer a self-contained PC-style pack. Never pair its index with a
  // different native family merely because a file is absent.
  family(root/"Data");family(root/"DATA");family(root/"Assets");
  // Preserve the supported console extraction layouts until the guest
  // archive file API itself is replaced.
  const std::array legacy{root/"DATA"/"XENON",root/"Data"/"Xenon",root/"XENON"};
  for(const auto& base:legacy)family(base);
  family(root);
  // Historical detached root index beside the game executable.
  for(auto name:{"INDEX.IND","indexXBOX360.ind","index(1).ind","index(2).ind"})
    for(const auto& base:legacy){add(root/name,base);add(root/name,base/"USENGLISH");add(root/name,base/"UKENGLISH");}
  return result;
}
inline std::filesystem::path OriginalWaveArchive(const OriginalArchiveLocation& location) {
  const auto direct=location.directory/"WAVEBANK.ARC";
  if(IsArchiveFile(direct))return direct;
  // Localized project banks may share the parent wave archive.
  const auto leaf=location.directory.filename();
  if(leaf=="USENGLISH" || leaf=="UKENGLISH"){
    const auto shared=location.directory.parent_path()/"WAVEBANK.ARC";
    if(IsArchiveFile(shared))return shared;
  }
  return {};
}
} // namespace hulk::eor::source
