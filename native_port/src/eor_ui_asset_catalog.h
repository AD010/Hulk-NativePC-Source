// Hulk 2008 original UI asset lookup, NOT a texture format converter.
// Native C++20: no guest PPC, Vulkan, Xenia or ReXGlue dependencies.
#pragma once

#include "eor_archive.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hulk::eor {

enum class OriginalPlatform { Pc2008, Xbox360 };
enum class KnownImageEncoding { Unknown, Dds, Png, Bmp };

struct OriginalArchiveSet {
  std::filesystem::path index;
  std::filesystem::path fonts;
  std::filesystem::path textures;
};

struct OriginalUiAsset {
  OriginalPlatform platform;
  std::string group;
  std::string name;
  std::filesystem::path archive;
  Resource record;
};

// Recognizes only headers actually present at byte 0. An unknown original
// EoR texture is NOT a DDS and cannot be fed straight to a PC image API.
KnownImageEncoding ClassifyImage(std::span<const std::byte> bytes) noexcept;

class OriginalUiAssets final {
 public:
  // Optional Xbox archive is a SOURCE for later verified conversion, never
  // silently substituted for incompatible PC material/texture records.
  static OriginalUiAssets Open(
      OriginalArchiveSet pc,
      std::optional<OriginalArchiveSet> xbox = std::nullopt);

  std::optional<OriginalUiAsset> Find(
      OriginalPlatform platform, std::string_view category,
      std::string_view original_asset_name) const;

  // Reads the actual user-owned original record. Does not copy assets to
  // the repository or change their byte order, tiling, compression or tags.
  std::vector<std::byte> Read(const OriginalUiAsset& asset,
                              size_t maximum = 256u << 20) const;

  bool HasXboxSource() const noexcept { return xbox_index_.has_value(); }

 private:
  OriginalUiAssets(OriginalArchiveSet pc, Index pc_index,
                   std::optional<OriginalArchiveSet> xbox,
                   std::optional<Index> xbox_index);

  OriginalArchiveSet pc_;
  Index pc_index_;
  std::optional<OriginalArchiveSet> xbox_;
  std::optional<Index> xbox_index_;
};
}  // namespace hulk::eor
