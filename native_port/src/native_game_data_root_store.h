// Atomic, SDK-independent writer for the ONE Hulk NativePC TOML path.
// No additional configuration file survives a successful update.
#pragma once
#include <filesystem>

namespace hulk::assets::config {
bool SaveGameDataRoot(const std::filesystem::path& config,
                      const std::filesystem::path& game_dir) noexcept;
}  // namespace hulk::assets::config
