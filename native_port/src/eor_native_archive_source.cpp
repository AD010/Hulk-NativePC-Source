#include "eor_native_archive_source.h"
#include "assets_locator.h"
#include <stdexcept>
#include <utility>
namespace hulk::eor::world {
namespace {
model::OriginalModelAssets OpenUsaModelArchive(
    const std::filesystem::path& index,
    const std::filesystem::path& archive) {
  // The generic EoR parser intentionally supports both Xbox regions and PC.
  // The independent *USA runtime* must NOT instantiate it through the older
  // EU/PAL Xbox IND even when that index has valid big-endian records.
  if (!hulk::assets::IsPinnedUsaIndex(index))
    throw std::invalid_argument(
        "Native Hulk USA world stream requires the pinned USA INDEX.IND");
  return model::OriginalModelAssets::Open(index, archive);
}
} // namespace
ArchiveModelSource::ArchiveModelSource(const std::filesystem::path& index,
  const std::filesystem::path& archive)
  : assets_(OpenUsaModelArchive(index,archive)) {}
std::optional<model::LoadedNativeGeometry> ArchiveModelSource::Load(uint32_t hash) {
  return assets_.LoadNativeGeometryHash(hash);
}
} // namespace hulk::eor::world
