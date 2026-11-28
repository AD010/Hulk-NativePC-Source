#include "../native_port/src/eor_original_archive_layout.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
using namespace hulk::eor::source;
namespace fs=std::filesystem;
static void Check(bool ok){if(!ok)std::abort();}
int main(){
  const auto root=fs::temp_directory_path()/("hulk-layout-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup {fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{root};
  const auto put=[&](const fs::path& p){fs::create_directories(p.parent_path());std::ofstream(p).put('x');};
  Check(OriginalArchiveLocations({}).empty());Check(OriginalArchiveLocations(root).empty());
  put(root/"Data"/"INDEX.IND");put(root/"Data"/"WAVEBANK.ARC");
  put(root/"DATA"/"XENON"/"USENGLISH"/"INDEX.IND");put(root/"DATA"/"XENON"/"WAVEBANK.ARC");
  put(root/"Assets"/"INDEX.IND");put(root/"index(2).ind");
  auto choices=OriginalArchiveLocations(root);
  Check(!choices.empty()&&choices.front()==OriginalArchiveLocation{root/"Data"/"INDEX.IND",root/"Data"});
  Check(OriginalWaveArchive(choices.front())==root/"Data"/"WAVEBANK.ARC");
  const auto has=[&](fs::path i,fs::path d){return std::find(choices.begin(),choices.end(),OriginalArchiveLocation{i,d})!=choices.end();};
  Check(has(root/"DATA"/"XENON"/"USENGLISH"/"INDEX.IND",root/"DATA"/"XENON"));
  Check(has(root/"index(2).ind",root/"DATA"/"XENON"));
  Check(!has(root/"Data"/"INDEX.IND",root/"Assets"));
  Check(!has(root/"Data"/"INDEX.IND",root/"DATA"/"XENON"));
  Check(OriginalWaveArchive({root/"DATA"/"XENON"/"USENGLISH"/"INDEX.IND",root/"DATA"/"XENON"/"USENGLISH"})==root/"DATA"/"XENON"/"WAVEBANK.ARC");
  Check(OriginalWaveArchive({root/"Assets"/"INDEX.IND",root/"Assets"}).empty());
  put(root/"Data"/"USENGLISH"/"INDEX.IND");
  choices=OriginalArchiveLocations(root);
  Check(has(root/"Data"/"USENGLISH"/"INDEX.IND",root/"Data"));
}
