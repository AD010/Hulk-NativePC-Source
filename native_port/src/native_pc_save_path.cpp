#include "native_pc_save_path.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#endif

namespace hulk::save {
std::optional<std::filesystem::path> FolderUnderDocuments(
    const std::filesystem::path& documents) {
  if (documents.empty()) return std::nullopt;
  return documents / "SEGA" / "The Incredible Hulk";
}

std::optional<std::filesystem::path> WindowsSaveFolder() {
#ifdef _WIN32
  PWSTR raw = nullptr;
  const HRESULT result = SHGetKnownFolderPath(
      FOLDERID_Documents, KF_FLAG_DONT_VERIFY, nullptr, &raw);
  if (FAILED(result) || !raw) {
    if (raw) CoTaskMemFree(raw);
    return std::nullopt;
  }
  const std::filesystem::path documents(raw);
  CoTaskMemFree(raw);
  return FolderUnderDocuments(documents);
#else
  return std::nullopt;
#endif
}
}  // namespace hulk::save
