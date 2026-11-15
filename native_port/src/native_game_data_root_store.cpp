#include "native_game_data_root_store.h"
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hulk::assets::config {
namespace {
constexpr std::uintmax_t kMaxConfig = 2u << 20;

bool Matches(std::string_view line) {
  // Only a live top-level assignment, not a comment or a similarly
  // prefixed variable. Basic TOML string parsing is unnecessary here.
  if (line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string_view::npos) return false;
  line.remove_prefix(start);
  constexpr std::string_view name{"game_data_root"};
  if (!line.starts_with(name)) return false;
  line.remove_prefix(name.size());
  const auto eq = line.find_first_not_of(" \t");
  return eq != std::string_view::npos && line[eq] == '=';
}

std::string Quote(const std::filesystem::path& dir) {
  const auto u8 = dir.generic_u8string();
  if (u8.empty() || u8.size() > 32767)
    throw std::invalid_argument("Invalid original game data directory");
  std::string quoted{"\""};
  for (auto item : u8) {
    const unsigned char c = static_cast<unsigned char>(item);
    if (c < 0x20 || c == 0x7f)
      throw std::invalid_argument("Unsafe control character in original data path");
    if (c == '"' || c == '\\') quoted.push_back('\\');
    quoted.push_back(static_cast<char>(c));
  }
  quoted.push_back('"');
  return quoted;
}
} // namespace

bool SaveGameDataRoot(const std::filesystem::path& config,
                      const std::filesystem::path& game_dir) noexcept {
  try {
    if (config.empty() || game_dir.empty()) return false;
    const auto quoted = Quote(game_dir);
    std::error_code ec;
    const bool exists = std::filesystem::exists(config, ec);
    if (ec || (exists && (!std::filesystem::is_regular_file(config, ec) ||
                        std::filesystem::file_size(config, ec) > kMaxConfig || ec)))
      return false;
    std::string original;
    if (exists) {
      std::ifstream in(config, std::ios::binary);
      if (!in) return false;
      original.assign(std::istreambuf_iterator<char>(in), {});
      if (in.bad()) return false;
      in.close(); // Windows: close the old handle before MoveFileExW.
      if (!in) return false;
    }
    const bool bom = original.starts_with("\xEF\xBB\xBF");
    const std::string newline =
        original.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string written;
    written.reserve(original.size() + quoted.size() + 48);
    if (bom) written += "\xEF\xBB\xBF";
    written += "game_data_root = " + quoted + newline;
    std::istringstream lines(bom ? original.substr(3) : original);
    std::string line;
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (Matches(line)) continue;
      written += line + newline;
    }
    if (written == original) return true;
    if (written.size() > kMaxConfig) return false;
    auto temporary = config;
    temporary += ".nativepc-root-writing";
    // The temporary file lives on the same volume as the destination.
    // Never truncate the existing TOML until the entire edit is valid.
    {
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      out.write(written.data(), static_cast<std::streamsize>(written.size()));
      out.flush();
      const bool written = out.good();
      out.close();
      if (!written || !out.good()) {
        std::filesystem::remove(temporary, ec);
        return false;
      }
    }
#ifdef _WIN32
    const bool replaced = MoveFileExW(
        temporary.c_str(), config.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, config, ec);
    const bool replaced = !ec;
#endif
    if (!replaced) std::filesystem::remove(temporary, ec);
    return replaced;
  } catch (...) {
    return false;
  }
}
}  // namespace hulk::assets::config
