// NativePC options, persisted in the existing single incredible_hulk.toml.
// Independently usable by a future non-ReXGlue EoR host.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace hulk::options {

// Fail-closed: only known UI options and validated values are accepted.
// Save the current config atomically on the same volume. Never creates
// another permanent settings file and preserves unrelated TOML keys.
bool Save(const std::filesystem::path& config_file,
          std::string_view key, std::string_view value) noexcept;

// Save BOTH physical window dimensions as a single transaction. They
// never modify the guest 1280x720 video mode or its Xenos XDK pitches.
bool SaveWindowSize(const std::filesystem::path& config_file,
                    uint32_t width, uint32_t height) noexcept;

// The original EoR options menu and the host settings share one resolved
// configuration. Persist only the axes the game actually changed.
void SetLiveConfigPath(const std::filesystem::path& path) noexcept;
bool SaveOriginalSubtitles(bool enabled) noexcept;
bool SaveOriginalLookInversion(std::optional<bool> yaw,
                               std::optional<bool> pitch) noexcept;

}  // namespace hulk::options
