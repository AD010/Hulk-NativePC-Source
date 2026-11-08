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
#include <bcrypt.h>
#endif

namespace hulk::assets {
namespace {

#if defined(_WIN32)
// Never select PAL game data for this SHA-pinned USA recomp.
bool MatchesSha256(const std::filesystem::path& file, std::string_view expected) {
  std::ifstream stream(file, std::ios::binary);
  if (!stream) return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
  bool good = false;
  do {
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm, &hash, nullptr, 0, nullptr, 0, 0))) break;
    std::array<unsigned char, 65536> bytes{};
    bool hash_error = false;
    while (stream) {
      stream.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
      const auto n = stream.gcount();
      if (n && !BCRYPT_SUCCESS(BCryptHashData(
                   hash, bytes.data(), static_cast<ULONG>(n), 0))) {
        hash_error = true;
        break;
      }
    }
    if (hash_error || stream.bad()) break;
    std::array<unsigned char, 32> digest{};
    if (!BCRYPT_SUCCESS(BCryptFinishHash(
            hash, digest.data(), static_cast<ULONG>(digest.size()), 0))) break;
    constexpr char hex[] = "0123456789abcdef";
    std::string actual;
    actual.reserve(64);
    for (const auto byte : digest) {
      actual.push_back(hex[byte >> 4]);
      actual.push_back(hex[byte & 15]);
    }
    good = actual == expected;
  } while (false);
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return good;
}
bool IsUsXex(const std::filesystem::path& file) {
  return MatchesSha256(file,
      "346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0");
}
// The user's USA global index differs from the older Xbox index by
// 4250 resource entries. Localized indexes may legitimately differ.
void AuditUsIndex(const std::filesystem::path& root) {
  constexpr std::string_view known_us =
      "ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d";
  constexpr std::string_view prior =
      "46e6c6ec9f7428ba3a1737ab7ebad564c1b027b006c3e35156d2e036ab8b2820";
  const std::array<std::filesystem::path, 3> candidates = {
      root / "DATA" / "XENON" / "INDEX.IND",
      root / "DATA" / "XENON" / "USENGLISH" / "INDEX.IND",
      root / "DATA" / "XENON" / "UKENGLISH" / "INDEX.IND"};
  for (const auto& file : candidates) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) continue;
    if (MatchesSha256(file, known_us)) {
      REXLOG_INFO("[HULK/US] User USA INDEX.IND verified: {}", file.string());
    } else if (MatchesSha256(file, prior)) {
      REXLOG_WARN("[HULK/US] Older Xbox INDEX.IND detected, 4250 data "
                  "differences versus user's USA index: {}", file.string());
    } else {
      REXLOG_INFO("[HULK/US] Additional regional INDEX.IND: {}", file.string());
    }
  }
}
#endif

bool ContainsXex(const std::filesystem::path& folder) {
  std::error_code ec;
  const auto xex = folder / "default.xex";
  if (folder.empty() || !std::filesystem::is_regular_file(xex, ec))
    return false;
#if defined(_WIN32)
  return IsUsXex(xex);
#else
  return true;
#endif
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
      L"Select The Incredible Hulk USA Xbox 360 default.xex";
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
                L"Select the original USA default.xex; this recomp cannot use a PAL XEX.",
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
    if (ContainsXex(candidate)) {
#if defined(_WIN32)
      AuditUsIndex(candidate);
#endif
      return candidate;
    }
    REXLOG_WARN("[HULK/US] Data root absent or XEX does not match pinned USA SHA256: {}",
                candidate.string());
  }
  const auto preferred_dir = ToFolder(preferred);
  if (ContainsXex(preferred_dir)) {
#if defined(_WIN32)
    AuditUsIndex(preferred_dir);
#endif
    return preferred_dir;
  }

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
  AuditUsIndex(folder);

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
