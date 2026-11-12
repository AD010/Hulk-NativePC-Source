#pragma once
// Hulk save package root for Windows. The guest payload stays in its
// original byte format until a verified PC serializer exists.
// Keep the original 2008 PC HULK.sav strictly outside this namespace.
#include <cstdint>
#include <filesystem>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#ifdef _MSC_VER
#pragma comment(lib, "Shell32.lib")
#endif
#endif

namespace hulk::native_save {
inline constexpr uint32_t kHulkTitle = 0x534507F1u;
inline constexpr uint32_t kSavedGame = 0x00000001u;

inline std::filesystem::path RootUnderDocuments(
    const std::filesystem::path& documents, uint32_t title, uint32_t type) {
  if (documents.empty() || title != kHulkTitle || type != kSavedGame)
    return {};
  // Never collide with retail HULK.sav, or mix Xbox user IDs into paths.
  return documents / "SEGA" / "The Incredible Hulk" / "NativePC" /
         "SavedGames";
}

inline std::filesystem::path WindowsRoot(uint32_t title, uint32_t type,
                                         bool enabled) {
  if (!enabled || title != kHulkTitle || type != kSavedGame) return {};
#ifdef _WIN32
  PWSTR documents = nullptr;
  const HRESULT hr = SHGetKnownFolderPath(
      FOLDERID_Documents, KF_FLAG_DONT_VERIFY, nullptr, &documents);
  if (FAILED(hr) || !documents) {
    if (documents) CoTaskMemFree(documents);
    return {};
  }
  const std::filesystem::path root = RootUnderDocuments(
      std::filesystem::path(documents), title, type);
  CoTaskMemFree(documents);
  return root;
#else
  return {};
#endif
}
}  // namespace hulk::native_save
