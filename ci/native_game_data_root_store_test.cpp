// Synthetic regression for preserving one Hulk TOML, UTF-8, CRLF and
// Documents/OneDrive or other original asset roots.
#include "../native_port/src/native_game_data_root_store.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
using hulk::assets::config::SaveGameDataRoot;
namespace fs = std::filesystem;
static std::string Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}
static void Write(const fs::path& path,const std::string& content) {
  std::ofstream stream(path,std::ios::binary|std::ios::trunc);
  stream.write(content.data(),static_cast<std::streamsize>(content.size()));
}
int main() {
  auto root = fs::temp_directory_path() / "hulk_nativepc_root_test";
  std::error_code ec;fs::remove_all(root,ec);fs::create_directories(root);
  const auto file = root / "incredible_hulk.toml";
  Write(file,"\xef\xbb\xbf# config\r\n"
             "game_data_root = \"C:/Old Game\"\r\n"
             "# game_data_root = \"comment\"\r\n"
             "fullscreen = true\r\n"
             "game_data_root   = \"D:/older\"\r\n");
  assert(SaveGameDataRoot(file,fs::path("E:/Games/The Incredible Hulk")));
  const auto result = Read(file);
  assert(result.starts_with("\xef\xbb\xbfgame_data_root = \"E:/Games/The Incredible Hulk\"\r\n"));
  assert(result.find("fullscreen = true\r\n")!=std::string::npos);
  assert(result.find("# game_data_root = \"comment\"\r\n")!=std::string::npos);
  assert(result.find("D:/older")==std::string::npos);
  assert(result.find("C:/Old Game")==std::string::npos);
  assert(!fs::exists(fs::path(file.string()+".nativepc-root-writing")));
  assert(SaveGameDataRoot(file,fs::path("E:/Games/The Incredible Hulk")));
  assert(Read(file)==result);
  const fs::path cyrillic = fs::u8path(u8"C:/Игры/Халк USA");
  assert(SaveGameDataRoot(file,cyrillic));
  assert(Read(file).find("Халк USA")!=std::string::npos);
  assert(!SaveGameDataRoot(file,fs::path{}));
  assert(Read(file).find("Халк USA")!=std::string::npos);
  assert(!SaveGameDataRoot(root/"no_such_parent"/"incredible_hulk.toml",cyrillic));
  assert(SaveGameDataRoot(root/"new.toml",cyrillic));
  assert(Read(root/"new.toml").starts_with("game_data_root = "));
  fs::remove_all(root,ec);
}
