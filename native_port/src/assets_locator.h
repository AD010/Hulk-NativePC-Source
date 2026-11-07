#pragma once
#include <filesystem>
#include <optional>

namespace hulk::assets {

// Called from ReXApp::OnFinalizePaths after the single TOML is loaded.
// An existing valid game directory is used directly. If none is found, on
// Windows a native file picker asks for the user's own default.xex once and
// writes its containing directory into the same TOML; no launcher required.
std::optional<std::filesystem::path> LocateOrSelectGameData(
    const std::filesystem::path& configured_dir,
    const std::filesystem::path& config_toml);
}  // namespace hulk::assets
