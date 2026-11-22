#include "eor_original_texture_assets.h"
namespace hulk::eor::textures {
OriginalXboxTextureAssets OriginalXboxTextureAssets::OpenPinnedUsa(
    const std::filesystem::path& index,
    const std::filesystem::path& texture_archive,
    const std::filesystem::path& streams_archive) {
  auto verified = OpenIndexed(Index::LoadPinnedUsa(index),texture_archive,streams_archive);
  verified.pinned_usa_index_ = true;
  return verified;
}
} // namespace hulk::eor::textures
