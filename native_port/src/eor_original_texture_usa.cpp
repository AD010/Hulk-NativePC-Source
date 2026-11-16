#include "eor_original_texture_assets.h"
#include "assets_locator.h"
#include <stdexcept>
namespace hulk::eor::textures {
OriginalXboxTextureAssets OriginalXboxTextureAssets::OpenPinnedUsa(
    const std::filesystem::path& index,
    const std::filesystem::path& texture_archive,
    const std::filesystem::path& streams_archive) {
  if (!hulk::assets::IsPinnedUsaIndex(index))
    throw std::invalid_argument(
        "Native Hulk USA texture stream requires the pinned USA INDEX.IND");
  auto verified = Open(index, texture_archive, streams_archive);
  verified.pinned_usa_index_ = true;
  return verified;
}
} // namespace hulk::eor::textures
