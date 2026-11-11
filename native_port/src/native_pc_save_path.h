#pragma once
#include <filesystem>
#include <optional>

namespace hulk::save {
// Intended final PC folder from the official The Incredible Hulk Windows
// README. This does not convert XContent containers or install a save hook.
std::optional<std::filesystem::path> FolderUnderDocuments(
    const std::filesystem::path& documents);

// Resolve the real Windows Documents folder, including redirected/OneDrive.
// Does not create directories or write files.
std::optional<std::filesystem::path> WindowsSaveFolder();
}
