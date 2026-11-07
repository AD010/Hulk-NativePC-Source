// Native installation-data discovery. No launch scripts and no extra user files.
#include "assets_locator.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace hulk::assets {
namespace {

bool ContainsXex(const std::filesystem::path& folder) {
  std::error_code ec;
  return !folder.empty() &&
      std::filesystem::is_regular_file(folder / "default.xex", ec);
}

std::filesystem::path ToFolder(std::filesystem::path path) {
  if (path.filename() == "default.xex") path = path.parent_path();
  return path;
}

std::string Utf8Generic(const std::filesystem::path& path) {
  const auto u8 = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// TOML basic string with forward slashes instead of Windows '\\' escapes.
std::string TomlQuotedPath(const std::filesystem::path& path) {
  const auto utf8 = Utf8Generic(path);
  std::string result = "\"";
  for (const unsigned char c : utf8) {
    if (c == '"' || c == '\\') {
      result.push_back('\\');
      result.push_back(static_cast<char>(c));
    } else if (c >= 0x20) {
      result.push_back(static_cast<char>(c));
    }
  }
  result.push_back('"');
  return result;
}

bool IsRootAssignment(std::string_view line) {
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string_view::npos) return false;
  line.remove_prefix(start);
  constexpr std::string_view key = "game_data_root";
  if (!line.starts_with(key)) return false;
  line.remove_prefix(key.size());
  const auto equal = line.find_first_not_of(" \t");
  return equal != std::string_view::npos && line[equal] == '=';
}

bool SaveInExistingToml(const std::filesystem::path& config,
                        const std::filesystem::path& folder) {
  std::string old;
  {
    std::ifstream in(config, std::ios::binary);
    if (in) old.assign(std::istreambuf_iterator<char>(in), {});
  }
  // Retain UTF-8 BOM only at the beginning; prepend one canonical value.
  std::string content;
  if (old.starts_with("\xEF\xBB\xBF")) {
    content.append(old, 0, 3);
    old.erase(0, 3);
  }
  content += "game_data_root = " + TomlQuotedPath(folder) + "\n";

  std::istringstream lines(old);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (IsRootAssignment(line)) continue;  // avoid duplicate keys
    content += line;
    content.push_back('\n');
  }
  std::ofstream out(config, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  out.flush();
  return out.good();
}

#if defined(_WIN32)
std::optional<std::filesystem::path> SelectDefaultXex() {
  std::array<wchar_t, 32768> result{};
  OPENFILENAMEW request{};
  request.lStructSize = sizeof(request);
  request.lpstrFilter =
      L"Xbox 360 default.xex\0default.xex\0All files\0*.*\0\0";
  request.lpstrFile = result.data();
  request.nMaxFile = static_cast<DWORD>(result.size());
  request.lpstrTitle =
      L"Select The Incredible Hulk (Xbox 360) - default.xex";
  request.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                  OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetOpenFileNameW(&request)) {
    // Cancel is not an error: the user can reopen the game later.
    const auto code = CommDlgExtendedError();
    if (code != 0) {
      MessageBoxW(nullptr, L"Could not open the Windows file picker.",
                  L"Hulk NativePC", MB_OK | MB_ICONERROR);
    }
    return std::nullopt;
  }
  const std::filesystem::path selected(result.data());
  if (_wcsicmp(selected.filename().c_str(), L"default.xex") != 0 ||
      !ContainsXex(selected.parent_path())) {
    MessageBoxW(nullptr,
                L"Please select the original Xbox 360 default.xex.",
                L"Hulk NativePC", MB_OK | MB_ICONWARNING);
    return std::nullopt;
  }
  return selected.parent_path();
}
#endif
}  // namespace

std::optional<std::filesystem::path> LocateOrSelectGameData(
    const std::filesystem::path& preferred,
    const std::filesystem::path& config_toml) {
  // The ReXGlue default paths were computed before loading its TOML. Read
  // the current cvar to honor the user-configured value from that TOML.
  const auto from_toml = rex::cvar::Query<std::string>("game_data_root");
  if (!from_toml.empty()) {
    const auto candidate = ToFolder(rex::to_path(from_toml));
    if (ContainsXex(candidate)) return candidate;
    REXLOG_WARN("[HULK/ASSETS] Configured folder has no default.xex: {}",
                candidate.string());
  }
  const auto preferred_dir = ToFolder(preferred);
  if (ContainsXex(preferred_dir)) return preferred_dir;

  const auto exe = rex::filesystem::GetExecutableFolder();
  const std::array<std::filesystem::path, 4> candidates = {
      exe / "game", exe, exe.parent_path() / "game", exe.parent_path()};
  for (const auto& candidate : candidates) {
    if (ContainsXex(candidate)) return candidate;
  }

#if defined(_WIN32)
  const auto selected = SelectDefaultXex();
  if (!selected) return std::nullopt;
  const auto folder = std::filesystem::absolute(*selected);

  // Save into the existing and ONLY TOML, never a sidecar or new script.
  // Sync the live cvar too, so ReXGlue's F4 "Save" doesn't lose the path.
  (void)rex::cvar::SetFlagByName("game_data_root", Utf8Generic(folder));
  if (!SaveInExistingToml(config_toml, folder)) {
    REXLOG_WARN("[HULK/ASSETS] Could not save directory to {}",
                config_toml.string());
    MessageBoxW(nullptr,
        L"The game will launch, but Windows could not save the asset path."
        L" Please check write permissions for incredible_hulk.toml.",
        L"Hulk NativePC", MB_OK | MB_ICONWARNING);
  } else {
    REXLOG_INFO("[HULK/ASSETS] Saved game data root in incredible_hulk.toml");
  }
  return folder;
#else
  return std::nullopt;
#endif
}
}  // namespace hulk::assets
