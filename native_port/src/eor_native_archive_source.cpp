#include "eor_native_archive_source.h"
namespace hulk::eor::world {
ArchiveModelSource::ArchiveModelSource(const std::filesystem::path& index,
  const std::filesystem::path& archive)
  : assets_(model::OriginalModelAssets::Open(Index::LoadPinnedUsa(index),archive)) {}
std::optional<model::LoadedNativeGeometry> ArchiveModelSource::Load(uint32_t hash) {
  return assets_.LoadNativeGeometryHash(hash);
}
} // namespace hulk::eor::world
